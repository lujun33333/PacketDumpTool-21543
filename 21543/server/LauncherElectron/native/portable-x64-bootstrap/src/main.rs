#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

use portable_x64_bootstrap::archive::{extract_archive, read_archive};
use std::env;
use std::ffi::c_void;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

const INNER_EXECUTABLE: &str = "QQSpeedLauncher.exe";
const PORTABLE_APP_FILENAME: &str = "QQSpeedLauncher";

#[derive(Debug)]
struct BootstrapError {
    stage: &'static str,
    message: String,
}

impl BootstrapError {
    fn new(stage: &'static str, message: impl Into<String>) -> Self {
        Self {
            stage,
            message: message.into(),
        }
    }
}

fn unix_time_millis() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |duration| duration.as_millis())
}

fn sanitize_log_value(value: &str) -> String {
    value
        .chars()
        .take(768)
        .map(|character| match character {
            '\r' | '\n' | '\t' => ' ',
            character if character.is_control() => '?',
            character => character,
        })
        .collect()
}

fn log_path() -> Option<PathBuf> {
    let local_app_data = env::var_os("LOCALAPPDATA")?;
    Some(
        PathBuf::from(local_app_data)
            .join("QQSpeedLauncher")
            .join("logs")
            .join("portable-x64.log"),
    )
}

fn write_log(event: &str, detail: &str, elapsed: Duration) {
    let Some(path) = log_path() else {
        return;
    };
    let Some(parent) = path.parent() else {
        return;
    };
    if fs::create_dir_all(parent).is_err() {
        return;
    }
    if path
        .metadata()
        .is_ok_and(|metadata| metadata.len() > 512 * 1024)
    {
        let backup = path.with_extension("log.1");
        let _ignored = fs::remove_file(&backup);
        let _ignored = fs::rename(&path, backup);
    }
    let Ok(mut file) = OpenOptions::new().create(true).append(true).open(path) else {
        return;
    };
    let line = format!(
        "ts={} pid={} event={} elapsed_ms={} detail={}\n",
        unix_time_millis(),
        std::process::id(),
        sanitize_log_value(event),
        elapsed.as_millis(),
        sanitize_log_value(detail)
    );
    let _ignored = file.write_all(line.as_bytes());
}

fn create_unique_temp_dir() -> Result<PathBuf, BootstrapError> {
    let base = env::temp_dir();
    let nonce = unix_time_millis();
    for attempt in 0_u32..128 {
        let candidate = base.join(format!(
            "QQSpeedLauncher-{}-{nonce:x}-{attempt:02x}",
            std::process::id()
        ));
        match fs::create_dir(&candidate) {
            Ok(()) => return Ok(candidate),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => {
                return Err(BootstrapError::new(
                    "TEMP_CREATE_FAILED",
                    format!("Windows error: {error}"),
                ));
            }
        }
    }
    Err(BootstrapError::new(
        "TEMP_CREATE_FAILED",
        "unable to allocate a unique temporary directory",
    ))
}

fn cleanup_temp_dir(path: &Path) -> Result<(), std::io::Error> {
    let delays = [0_u64, 50, 100, 200, 400, 800];
    let mut last_error = None;
    for delay in delays {
        if delay > 0 {
            thread::sleep(Duration::from_millis(delay));
        }
        match fs::remove_dir_all(path) {
            Ok(()) => return Ok(()),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(()),
            Err(error) => last_error = Some(error),
        }
    }
    Err(last_error.unwrap_or_else(|| std::io::Error::other("temporary cleanup failed")))
}

fn run_bootstrap(started: Instant) -> Result<i32, BootstrapError> {
    let current_executable = env::current_exe().map_err(|error| {
        BootstrapError::new("SELF_PATH_FAILED", format!("Windows error: {error}"))
    })?;
    let portable_directory = current_executable.parent().ok_or_else(|| {
        BootstrapError::new(
            "SELF_PATH_FAILED",
            "portable executable has no parent directory",
        )
    })?;

    let mut executable = File::open(&current_executable).map_err(|error| {
        BootstrapError::new("SELF_OPEN_FAILED", format!("Windows error: {error}"))
    })?;
    let archive = read_archive(&mut executable)
        .map_err(|error| BootstrapError::new("ARCHIVE_VERIFY_FAILED", error.to_string()))?;
    write_log(
        "ARCHIVE_VERIFIED",
        &format!("entries={}", archive.entries.len()),
        started.elapsed(),
    );

    let extraction_root = create_unique_temp_dir()?;
    write_log("TEMP_CREATED", "ok", started.elapsed());

    let extraction_result = extract_archive(&mut executable, &archive, &extraction_root)
        .map_err(|error| BootstrapError::new("ARCHIVE_EXTRACT_FAILED", error.to_string()));

    if let Err(error) = extraction_result {
        let _ignored = cleanup_temp_dir(&extraction_root);
        return Err(error);
    }
    write_log("ARCHIVE_EXTRACTED", "ok", started.elapsed());

    let inner_executable = extraction_root.join(INNER_EXECUTABLE);
    if !inner_executable.is_file() {
        let _ignored = cleanup_temp_dir(&extraction_root);
        return Err(BootstrapError::new(
            "INNER_MISSING",
            "the packaged QQSpeedLauncher.exe is missing",
        ));
    }

    let child_result = Command::new(&inner_executable)
        .args(env::args_os().skip(1))
        .current_dir(&extraction_root)
        .env("PORTABLE_EXECUTABLE_DIR", portable_directory)
        .env("PORTABLE_EXECUTABLE_FILE", &current_executable)
        .env("PORTABLE_EXECUTABLE_APP_FILENAME", PORTABLE_APP_FILENAME)
        .spawn();
    let mut child = match child_result {
        Ok(child) => child,
        Err(error) => {
            let _ignored = cleanup_temp_dir(&extraction_root);
            return Err(BootstrapError::new(
                "INNER_START_FAILED",
                format!("Windows error: {error}"),
            ));
        }
    };
    write_log(
        "INNER_STARTED",
        &format!("pid={}", child.id()),
        started.elapsed(),
    );

    let status = child.wait().map_err(|error| {
        BootstrapError::new("INNER_WAIT_FAILED", format!("Windows error: {error}"))
    });
    let cleanup_result = cleanup_temp_dir(&extraction_root);
    if let Err(error) = cleanup_result {
        write_log(
            "TEMP_CLEANUP_FAILED",
            &format!("Windows error: {error}"),
            started.elapsed(),
        );
    } else {
        write_log("TEMP_CLEANED", "ok", started.elapsed());
    }

    let status = status?;
    let exit_code = status.code().unwrap_or(1);
    write_log(
        "INNER_EXITED",
        &format!("exit_code={exit_code}"),
        started.elapsed(),
    );
    Ok(exit_code)
}

#[cfg(target_os = "windows")]
#[link(name = "user32")]
unsafe extern "system" {
    fn MessageBoxW(
        window: *mut c_void,
        text: *const u16,
        caption: *const u16,
        message_type: u32,
    ) -> i32;
}

#[cfg(target_os = "windows")]
fn show_error(message: &str) {
    const MB_OK: u32 = 0x0000;
    const MB_ICONERROR: u32 = 0x0010;
    const MB_SETFOREGROUND: u32 = 0x0001_0000;
    let text: Vec<u16> =
        format!("登录器启动失败。\n\n{message}\n\n详细信息已写入 portable-x64.log。")
            .encode_utf16()
            .chain(std::iter::once(0))
            .collect();
    let caption: Vec<u16> = "QQ飞车登录器".encode_utf16().chain(std::iter::once(0)).collect();
    // SAFETY: Both UTF-16 buffers are NUL-terminated and remain alive for the duration of the call.
    // A null owner HWND is explicitly supported by MessageBoxW, and the flags contain no callbacks.
    unsafe {
        MessageBoxW(
            std::ptr::null_mut(),
            text.as_ptr(),
            caption.as_ptr(),
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND,
        );
    }
}

#[cfg(not(target_os = "windows"))]
fn show_error(message: &str) {
    eprintln!("QQSpeedLauncher: {message}");
}

fn main() {
    let started = Instant::now();
    write_log("OUTER_BEGIN", "arch=x64", started.elapsed());
    match run_bootstrap(started) {
        Ok(exit_code) => std::process::exit(exit_code),
        Err(error) => {
            write_log(error.stage, &error.message, started.elapsed());
            show_error(&format!("阶段：{}\n错误：{}", error.stage, error.message));
            std::process::exit(1);
        }
    }
}
