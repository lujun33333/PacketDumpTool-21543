use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::error::Error;
use std::fmt::{Display, Formatter};
use std::fs::{self, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Component, Path, PathBuf};

pub const FOOTER_MAGIC: &[u8; 16] = b"QQSPEED-X64-SFX\0";
pub const FOOTER_SIZE: u64 = 80;
const INDEX_ENTRY_HEADER_SIZE: usize = 56;
const MAX_ENTRY_COUNT: u32 = 4096;
const MAX_INDEX_SIZE: u64 = 16 * 1024 * 1024;
const MAX_PATH_BYTES: usize = 4096;
const COPY_BUFFER_SIZE: usize = 1024 * 1024;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub relative_path: PathBuf,
    pub offset: u64,
    pub length: u64,
    pub sha256: [u8; 32],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Archive {
    pub entries: Vec<Entry>,
    pub payload_start: u64,
    pub index_offset: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ArchiveError(String);

impl ArchiveError {
    fn new(message: impl Into<String>) -> Self {
        Self(message.into())
    }
}

impl Display for ArchiveError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl Error for ArchiveError {}

fn read_exact_at<R: Read + Seek>(
    reader: &mut R,
    position: u64,
    buffer: &mut [u8],
) -> Result<(), ArchiveError> {
    reader
        .seek(SeekFrom::Start(position))
        .map_err(|error| ArchiveError::new(format!("archive seek failed: {error}")))?;
    reader
        .read_exact(buffer)
        .map_err(|error| ArchiveError::new(format!("archive read failed: {error}")))
}

fn u16_at(bytes: &[u8], offset: usize) -> Result<u16, ArchiveError> {
    let value = bytes
        .get(offset..offset + 2)
        .ok_or_else(|| ArchiveError::new("index field is out of range"))?;
    Ok(u16::from_le_bytes([value[0], value[1]]))
}

fn u32_at(bytes: &[u8], offset: usize) -> Result<u32, ArchiveError> {
    let value = bytes
        .get(offset..offset + 4)
        .ok_or_else(|| ArchiveError::new("index field is out of range"))?;
    Ok(u32::from_le_bytes(
        value
            .try_into()
            .map_err(|_| ArchiveError::new("invalid u32 field"))?,
    ))
}

fn u64_at(bytes: &[u8], offset: usize) -> Result<u64, ArchiveError> {
    let value = bytes
        .get(offset..offset + 8)
        .ok_or_else(|| ArchiveError::new("index field is out of range"))?;
    Ok(u64::from_le_bytes(
        value
            .try_into()
            .map_err(|_| ArchiveError::new("invalid u64 field"))?,
    ))
}

fn is_reserved_windows_name(segment: &str) -> bool {
    let stem = segment.split('.').next().unwrap_or_default();
    let upper = stem.to_ascii_uppercase();
    matches!(upper.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || (upper.len() == 4
            && (upper.starts_with("COM") || upper.starts_with("LPT"))
            && matches!(upper.as_bytes()[3], b'1'..=b'9'))
}

fn validate_relative_path(raw: &str) -> Result<PathBuf, ArchiveError> {
    if raw.is_empty()
        || raw.len() > MAX_PATH_BYTES
        || raw.starts_with('/')
        || raw.contains('\\')
        || raw.contains('\0')
        || raw.contains(':')
    {
        return Err(ArchiveError::new(
            "archive path is not a safe relative path",
        ));
    }

    for segment in raw.split('/') {
        if segment.is_empty()
            || segment == "."
            || segment == ".."
            || segment.ends_with([' ', '.'])
            || segment.chars().any(|character| {
                character.is_control() || matches!(character, '<' | '>' | '"' | '|' | '?' | '*')
            })
            || is_reserved_windows_name(segment)
        {
            return Err(ArchiveError::new(
                "archive path contains an unsafe path segment",
            ));
        }
    }

    let path = PathBuf::from(raw.replace('/', std::path::MAIN_SEPARATOR_STR));
    if path.components().any(|component| {
        matches!(
            component,
            Component::Prefix(_) | Component::RootDir | Component::ParentDir | Component::CurDir
        )
    }) {
        return Err(ArchiveError::new(
            "archive path escaped the extraction root",
        ));
    }
    Ok(path)
}

pub fn read_archive<R: Read + Seek>(reader: &mut R) -> Result<Archive, ArchiveError> {
    let file_size = reader
        .seek(SeekFrom::End(0))
        .map_err(|error| ArchiveError::new(format!("archive size read failed: {error}")))?;
    if file_size <= FOOTER_SIZE {
        return Err(ArchiveError::new("archive is smaller than the SFX footer"));
    }

    let footer_offset = file_size - FOOTER_SIZE;
    let mut footer = [0_u8; FOOTER_SIZE as usize];
    read_exact_at(reader, footer_offset, &mut footer)?;
    if footer.get(0..16) != Some(FOOTER_MAGIC.as_slice()) {
        return Err(ArchiveError::new("archive footer magic mismatch"));
    }

    let version = u32_at(&footer, 16)?;
    if version != 1 {
        return Err(ArchiveError::new(format!(
            "archive version is not supported: {version}"
        )));
    }
    let entry_count = u32_at(&footer, 20)?;
    let index_offset = u64_at(&footer, 24)?;
    let index_size = u64_at(&footer, 32)?;
    let payload_start = u64_at(&footer, 72)?;
    let expected_index_hash: [u8; 32] = footer[40..72]
        .try_into()
        .map_err(|_| ArchiveError::new("archive index hash field is invalid"))?;

    if entry_count == 0 || entry_count > MAX_ENTRY_COUNT {
        return Err(ArchiveError::new(format!(
            "archive entry count is invalid: {entry_count}"
        )));
    }
    if index_size == 0 || index_size > MAX_INDEX_SIZE {
        return Err(ArchiveError::new(format!(
            "archive index size is invalid: {index_size}"
        )));
    }
    if payload_start == 0 || payload_start >= index_offset {
        return Err(ArchiveError::new("archive payload range is invalid"));
    }
    if index_offset.checked_add(index_size) != Some(footer_offset) {
        return Err(ArchiveError::new(
            "archive index range does not end at the footer",
        ));
    }

    let index_length = usize::try_from(index_size)
        .map_err(|_| ArchiveError::new("archive index cannot fit in memory"))?;
    let mut index = vec![0_u8; index_length];
    read_exact_at(reader, index_offset, &mut index)?;
    let actual_index_hash: [u8; 32] = Sha256::digest(&index).into();
    if actual_index_hash != expected_index_hash {
        return Err(ArchiveError::new("archive index SHA-256 mismatch"));
    }

    let mut entries = Vec::with_capacity(entry_count as usize);
    let mut names = HashSet::with_capacity(entry_count as usize);
    let mut cursor = 0_usize;
    let mut previous_end = payload_start;
    for position in 0..entry_count {
        if cursor.checked_add(INDEX_ENTRY_HEADER_SIZE).is_none()
            || cursor + INDEX_ENTRY_HEADER_SIZE > index.len()
        {
            return Err(ArchiveError::new(format!(
                "archive index entry {position} header is truncated"
            )));
        }
        let path_length = usize::from(u16_at(&index, cursor)?);
        let flags = u16_at(&index, cursor + 2)?;
        let reserved = u32_at(&index, cursor + 4)?;
        let offset = u64_at(&index, cursor + 8)?;
        let length = u64_at(&index, cursor + 16)?;
        let sha256: [u8; 32] = index[cursor + 24..cursor + 56]
            .try_into()
            .map_err(|_| ArchiveError::new("archive entry SHA-256 is invalid"))?;
        cursor += INDEX_ENTRY_HEADER_SIZE;

        if path_length == 0 || path_length > MAX_PATH_BYTES || cursor + path_length > index.len() {
            return Err(ArchiveError::new(format!(
                "archive index entry {position} path is truncated"
            )));
        }
        if flags != 0 || reserved != 0 {
            return Err(ArchiveError::new(format!(
                "archive index entry {position} flags are invalid"
            )));
        }
        let raw_path = std::str::from_utf8(&index[cursor..cursor + path_length])
            .map_err(|_| ArchiveError::new("archive path is not valid UTF-8"))?;
        cursor += path_length;
        let relative_path = validate_relative_path(raw_path)?;
        let normalized_name = raw_path.to_lowercase();
        if !names.insert(normalized_name) {
            return Err(ArchiveError::new(format!(
                "archive contains a duplicate path: {raw_path}"
            )));
        }

        let end = offset
            .checked_add(length)
            .ok_or_else(|| ArchiveError::new("archive entry range overflow"))?;
        if offset != previous_end || end > index_offset {
            return Err(ArchiveError::new(format!(
                "archive entry range is invalid: {raw_path}"
            )));
        }
        previous_end = end;
        entries.push(Entry {
            relative_path,
            offset,
            length,
            sha256,
        });
    }

    if cursor != index.len() {
        return Err(ArchiveError::new("archive index has trailing bytes"));
    }
    if previous_end != index_offset {
        return Err(ArchiveError::new(
            "archive payload range has unregistered bytes",
        ));
    }

    Ok(Archive {
        entries,
        payload_start,
        index_offset,
    })
}

pub fn extract_archive<R: Read + Seek>(
    reader: &mut R,
    archive: &Archive,
    destination: &Path,
) -> Result<(), ArchiveError> {
    if !destination.is_dir() {
        return Err(ArchiveError::new(
            "archive extraction root is not a directory",
        ));
    }

    let mut buffer = vec![0_u8; COPY_BUFFER_SIZE];
    for entry in &archive.entries {
        let output_path = destination.join(&entry.relative_path);
        let parent = output_path
            .parent()
            .ok_or_else(|| ArchiveError::new("archive output path has no parent"))?;
        fs::create_dir_all(parent).map_err(|error| {
            ArchiveError::new(format!("archive output directory creation failed: {error}"))
        })?;
        let mut output = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&output_path)
            .map_err(|error| {
                ArchiveError::new(format!("archive output file creation failed: {error}"))
            })?;
        reader
            .seek(SeekFrom::Start(entry.offset))
            .map_err(|error| ArchiveError::new(format!("archive payload seek failed: {error}")))?;

        let mut remaining = entry.length;
        let mut hasher = Sha256::new();
        while remaining > 0 {
            let wanted = usize::try_from(remaining.min(buffer.len() as u64))
                .map_err(|_| ArchiveError::new("archive payload block is too large"))?;
            let count = reader.read(&mut buffer[..wanted]).map_err(|error| {
                ArchiveError::new(format!("archive payload read failed: {error}"))
            })?;
            if count == 0 {
                return Err(ArchiveError::new(
                    "archive payload ended before the declared length",
                ));
            }
            output.write_all(&buffer[..count]).map_err(|error| {
                ArchiveError::new(format!("archive output write failed: {error}"))
            })?;
            hasher.update(&buffer[..count]);
            remaining -= count as u64;
        }
        output
            .flush()
            .map_err(|error| ArchiveError::new(format!("archive output flush failed: {error}")))?;
        let actual_hash: [u8; 32] = hasher.finalize().into();
        if actual_hash != entry.sha256 {
            drop(output);
            let _ignored = fs::remove_file(&output_path);
            return Err(ArchiveError::new(format!(
                "archive payload SHA-256 mismatch: {}",
                entry.relative_path.display()
            )));
        }
    }
    Ok(())
}
