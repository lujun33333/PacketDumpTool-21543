use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn sdk_tool_candidates(tool: &str) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    for variable in ["QQSPEED_WINDOWS_RC", "RC"] {
        if let Some(value) = env::var_os(variable) {
            candidates.push(PathBuf::from(value));
        }
    }
    if let Some(value) = env::var_os("WindowsSdkVerBinPath") {
        let root = PathBuf::from(value);
        candidates.push(root.join("x64").join(tool));
        candidates.push(root.join(tool));
    }
    if let Some(program_files) = env::var_os("ProgramFiles(x86)") {
        let root = PathBuf::from(program_files)
            .join("Windows Kits")
            .join("10")
            .join("bin");
        if let Ok(entries) = fs::read_dir(root) {
            let mut versions = entries
                .flatten()
                .filter(|entry| entry.file_type().is_ok_and(|kind| kind.is_dir()))
                .map(|entry| entry.path())
                .collect::<Vec<_>>();
            versions.sort_by(|left, right| right.file_name().cmp(&left.file_name()));
            candidates.extend(
                versions
                    .into_iter()
                    .map(|version| version.join("x64").join(tool)),
            );
        }
    }
    candidates
}

fn find_resource_compiler() -> PathBuf {
    sdk_tool_candidates("rc.exe")
        .into_iter()
        .find(|candidate| candidate.is_file())
        .unwrap_or_else(|| PathBuf::from("rc.exe"))
}

fn compile_resources(manifest_dir: &Path, output: &Path) {
    let compiler = find_resource_compiler();
    let status = Command::new(&compiler)
        .current_dir(manifest_dir)
        .arg("/nologo")
        .arg(format!("/fo{}", output.display()))
        .arg("app.rc")
        .status()
        .unwrap_or_else(|error| panic!("failed to run {}: {error}", compiler.display()));
    assert!(
        status.success(),
        "{} failed with {status}",
        compiler.display()
    );
}

fn main() {
    println!("cargo:rerun-if-changed=app.rc");
    println!("cargo:rerun-if-changed=app.manifest");
    println!("cargo:rerun-if-changed=../../assets/app.ico");

    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let output = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR")).join("portable-x64.res");
    compile_resources(&manifest_dir, &output);
    println!(
        "cargo:rustc-link-arg-bin=portable-x64-bootstrap={}",
        output.display()
    );
}
