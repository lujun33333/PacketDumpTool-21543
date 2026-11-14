use portable_x64_bootstrap::archive::{extract_archive, read_archive};
use sha2::{Digest, Sha256};
use std::fs;
use std::io::Cursor;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

const MAGIC: &[u8; 16] = b"QQSPEED-X64-SFX\0";
const PREFIX_SIZE: usize = 128;

fn hash(data: &[u8]) -> [u8; 32] {
    Sha256::digest(data).into()
}

fn build_archive(entries: &[(&str, &[u8])]) -> Vec<u8> {
    let mut bytes = vec![0x5a; PREFIX_SIZE];
    let mut metadata = Vec::new();
    for (name, data) in entries {
        let offset = bytes.len() as u64;
        bytes.extend_from_slice(data);
        metadata.push((*name, offset, data.len() as u64, hash(data)));
    }

    let index_offset = bytes.len() as u64;
    let mut index = Vec::new();
    for (name, offset, length, digest) in metadata {
        let name = name.as_bytes();
        index.extend_from_slice(&(name.len() as u16).to_le_bytes());
        index.extend_from_slice(&0_u16.to_le_bytes());
        index.extend_from_slice(&0_u32.to_le_bytes());
        index.extend_from_slice(&offset.to_le_bytes());
        index.extend_from_slice(&length.to_le_bytes());
        index.extend_from_slice(&digest);
        index.extend_from_slice(name);
    }
    bytes.extend_from_slice(&index);
    bytes.extend_from_slice(MAGIC);
    bytes.extend_from_slice(&1_u32.to_le_bytes());
    bytes.extend_from_slice(&(entries.len() as u32).to_le_bytes());
    bytes.extend_from_slice(&index_offset.to_le_bytes());
    bytes.extend_from_slice(&(index.len() as u64).to_le_bytes());
    bytes.extend_from_slice(&hash(&index));
    bytes.extend_from_slice(&(PREFIX_SIZE as u64).to_le_bytes());
    bytes
}

fn unique_temp_dir(name: &str) -> PathBuf {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system time should be after epoch")
        .as_nanos();
    std::env::temp_dir().join(format!(
        "qqspeed-portable-x64-test-{name}-{}-{nonce}",
        std::process::id()
    ))
}

#[test]
fn reads_and_extracts_a_valid_archive() {
    let bytes = build_archive(&[
        ("QQSpeedLauncher.exe", b"launcher"),
        ("resources/app.asar", b"asar-data"),
    ]);
    let mut reader = Cursor::new(bytes);
    let archive = read_archive(&mut reader).expect("valid archive should parse");
    assert_eq!(archive.entries.len(), 2);
    assert_eq!(
        archive.entries[0].relative_path,
        PathBuf::from("QQSpeedLauncher.exe")
    );

    let destination = unique_temp_dir("valid");
    fs::create_dir(&destination).expect("test destination should be created");
    extract_archive(&mut reader, &archive, &destination).expect("valid archive should extract");
    assert_eq!(
        fs::read(destination.join("QQSpeedLauncher.exe")).unwrap(),
        b"launcher"
    );
    assert_eq!(
        fs::read(destination.join("resources/app.asar")).unwrap(),
        b"asar-data"
    );
    fs::remove_dir_all(destination).unwrap();
}

#[test]
fn rejects_bad_magic_and_version() {
    let mut bad_magic = build_archive(&[("QQSpeedLauncher.exe", b"launcher")]);
    let footer = bad_magic.len() - 80;
    bad_magic[footer] ^= 0xff;
    let error = read_archive(&mut Cursor::new(bad_magic)).unwrap_err();
    assert!(error.to_string().contains("magic"));

    let mut bad_version = build_archive(&[("QQSpeedLauncher.exe", b"launcher")]);
    let footer = bad_version.len() - 80;
    bad_version[footer + 16..footer + 20].copy_from_slice(&2_u32.to_le_bytes());
    let error = read_archive(&mut Cursor::new(bad_version)).unwrap_err();
    assert!(error.to_string().contains("version"));
}

#[test]
fn rejects_parent_absolute_empty_and_duplicate_paths() {
    for unsafe_name in [
        "../escape.dll",
        "/absolute.dll",
        "C:/drive.dll",
        "a//b.dll",
        "./same.dll",
    ] {
        let error =
            read_archive(&mut Cursor::new(build_archive(&[(unsafe_name, b"x")]))).unwrap_err();
        assert!(
            error.to_string().contains("path"),
            "unexpected error for {unsafe_name}: {error}"
        );
    }

    let error = read_archive(&mut Cursor::new(build_archive(&[
        ("resources/app.asar", b"a"),
        ("RESOURCES/app.asar", b"b"),
    ])))
    .unwrap_err();
    assert!(error.to_string().contains("duplicate"));
}

#[test]
fn rejects_index_range_tampering() {
    let mut bytes = build_archive(&[("QQSpeedLauncher.exe", b"launcher")]);
    let footer = bytes.len() - 80;
    let index_offset =
        u64::from_le_bytes(bytes[footer + 24..footer + 32].try_into().unwrap()) as usize;
    bytes[index_offset + 8..index_offset + 16].copy_from_slice(&1_u64.to_le_bytes());
    let index_size =
        u64::from_le_bytes(bytes[footer + 32..footer + 40].try_into().unwrap()) as usize;
    let new_hash = hash(&bytes[index_offset..index_offset + index_size]);
    bytes[footer + 40..footer + 72].copy_from_slice(&new_hash);
    let error = read_archive(&mut Cursor::new(bytes)).unwrap_err();
    assert!(error.to_string().contains("range"));
}

#[test]
fn rejects_payload_hash_mismatch_during_extraction() {
    let mut bytes = build_archive(&[("QQSpeedLauncher.exe", b"launcher")]);
    bytes[PREFIX_SIZE] ^= 0xff;
    let mut reader = Cursor::new(bytes);
    let archive = read_archive(&mut reader).expect("index should still parse");
    let destination = unique_temp_dir("bad-hash");
    fs::create_dir(&destination).unwrap();
    let error = extract_archive(&mut reader, &archive, &destination).unwrap_err();
    assert!(error.to_string().contains("SHA-256"));
    fs::remove_dir_all(destination).unwrap();
}
