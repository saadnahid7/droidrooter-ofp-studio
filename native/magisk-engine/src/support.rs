// SPDX-License-Identifier: GPL-3.0-only
// Strict ramdisk/payload validation adapted from PixelRootStation's native adapter.
use anyhow::{Context, Result, bail, ensure};
use avbroot::format::{
    compression::{CompressedFormat, CompressedReader, CompressedWriter},
    cpio::{self, CpioEntry, CpioEntryData, CpioEntryType, CpioReader},
    zip::{self, ZipEntriesSafeExt, ZipFileHeaderRecordExt},
};
use rawzip::{RECOMMENDED_BUFFER_SIZE, ZipArchive};
use std::{
    collections::HashMap,
    fs::File,
    io::{Cursor, Read},
    path::Path,
    sync::atomic::AtomicBool,
};
const MAX_RAMDISK: u64 = 64 * 1024 * 1024;
const PAYLOADS: [&str; 4] = [
    "lib/arm64-v8a/libmagiskinit.so",
    "lib/arm64-v8a/libmagisk.so",
    "lib/arm64-v8a/libinit-ld.so",
    "assets/util_functions.sh",
];
pub(crate) fn read_payloads(path: &Path) -> Result<HashMap<String, Vec<u8>>> {
    let archive = ZipArchive::from_file(File::open(path)?, &mut vec![0; RECOMMENDED_BUFFER_SIZE])?;
    let mut buffer = vec![0; RECOMMENDED_BUFFER_SIZE];
    let mut source = archive.entries_safe(&mut buffer);
    let mut result = HashMap::new();
    while let Some((header, entry)) = source.next_entry()? {
        let name = header.file_path_utf8()?;
        if !PAYLOADS.contains(&name) {
            continue;
        }
        let mut data = Vec::new();
        zip::verifying_reader(&entry, header.compression_method())?
            .take(8_388_609)
            .read_to_end(&mut data)?;
        ensure!(
            !data.is_empty() && data.len() <= 8_388_608,
            "invalid Magisk payload size"
        );
        ensure!(
            result.insert(name.to_owned(), data).is_none(),
            "duplicate Magisk payload"
        );
    }
    for name in PAYLOADS {
        ensure!(
            result.contains_key(name),
            "required Magisk 31 payload missing: {name}"
        );
    }
    for name in &PAYLOADS[..3] {
        let b = &result[*name];
        ensure!(
            b.len() > 20
                && &b[..4] == b"\x7fELF"
                && b[4] == 2
                && b[5] == 1
                && u16::from_le_bytes([b[18], b[19]]) == 183,
            "Magisk payload is not arm64 ELF: {name}"
        );
    }
    ensure!(
        String::from_utf8(result[PAYLOADS[3]].clone())?
            .lines()
            .any(|l| l == "MAGISK_VER_CODE=31100"),
        "Magisk 31 payload version mismatch"
    );
    Ok(result)
}
// Prevent decoder read-ahead from hiding an additional compressed member. These small
// init_boot inputs favor strict boundary accounting over buffered input throughput.
struct ExactInput<'a>(Cursor<&'a [u8]>, &'a AtomicBool);
impl Read for ExactInput<'_> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        if self.1.load(std::sync::atomic::Ordering::Relaxed) {
            return Err(std::io::Error::new(std::io::ErrorKind::Other, "Cancelled"));
        }
        let n = buffer.len().min(1);
        self.0.read(&mut buffer[..n])
    }
}
impl std::io::Seek for ExactInput<'_> {
    fn seek(&mut self, position: std::io::SeekFrom) -> std::io::Result<u64> {
        std::io::Seek::seek(&mut self.0, position)
    }
}
pub(crate) fn load_entries(
    ramdisk: &[u8],
    cancel: &AtomicBool,
) -> Result<(Vec<CpioEntry>, CompressedFormat)> {
    let mut reader = CompressedReader::new(ExactInput(Cursor::new(ramdisk), cancel), false)?;
    let format = reader.format();
    let mut raw = Vec::new();
    ensure!(
        matches!(format, CompressedFormat::Gzip | CompressedFormat::Lz4Legacy),
        "unsupported stock ramdisk compression; only gzip and legacy LZ4 are qualified by host fixtures"
    );
    if format == CompressedFormat::Lz4Legacy {
        // The upstream decoder considers partial block headers clean EOF. Validate the
        // complete legacy frame independently before allowing it to consume the input.
        let mut offset = 4usize;
        while offset < ramdisk.len() {
            ensure!(
                ramdisk.len() - offset >= 4,
                "truncated legacy LZ4 block header"
            );
            let size = u32::from_le_bytes(ramdisk[offset..offset + 4].try_into()?) as usize;
            offset += 4;
            ensure!(
                size > 0
                    && size <= 8_388_608 + 8_388_608 / 255 + 16
                    && size <= ramdisk.len() - offset,
                "invalid legacy LZ4 block bounds"
            );
            offset += size;
        }
    }
    Read::by_ref(&mut reader)
        .take(MAX_RAMDISK + 1)
        .read_to_end(&mut raw)?;
    ensure!(
        raw.len() as u64 <= MAX_RAMDISK,
        "ramdisk expansion limit exceeded"
    );
    let compressed = reader.into_inner().0;
    ensure!(
        compressed.get_ref()[compressed.position() as usize..]
            .iter()
            .all(|b| *b == 0),
        "unsupported trailing compressed ramdisk data"
    );
    let mut reader = CpioReader::new(Cursor::new(raw), false);
    let mut entries: Vec<CpioEntry> = Vec::new();
    let mut names = std::collections::HashMap::<Vec<u8>, usize>::new();
    while let Some(mut e) = reader.next_entry()? {
        ensure!(
            entries.len() < 4096
                && e.path.len() <= 4096
                && !e.path.starts_with(b"/")
                && !e.path.split(|c| *c == b'/').any(|p| p == b".."),
            "unsafe ramdisk entry"
        );
        if let CpioEntryData::Size(size) = e.data {
            ensure!(size <= 16_777_216, "ramdisk entry exceeds limit");
            let mut data = vec![0; size as usize];
            reader.read_exact(&mut data)?;
            e.data = CpioEntryData::Data(data);
        }
        if let Some(&index) = names.get(&e.path) {
            ensure!(
                same_empty_directory(&entries[index], &e),
                "duplicate ramdisk entry with conflicting attributes or non-directory contents"
            );
        } else {
            names.insert(e.path.clone(), entries.len());
        }
        // Preserve repeated directory records; never silently collapse stock entries.
        entries.push(e);
    }
    let raw = reader.into_inner();
    ensure!(
        raw.get_ref()[raw.position() as usize..]
            .iter()
            .all(|b| *b == 0),
        "unsupported nonzero trailing ramdisk data"
    );
    Ok((entries, format))
}
fn same_empty_directory(a: &CpioEntry, b: &CpioEntry) -> bool {
    if a.file_type != CpioEntryType::Directory
        || b.file_type != CpioEntryType::Directory
        || a.data.size().ok() != Some(0)
        || b.data.size().ok() != Some(0)
    {
        return false;
    }
    let mut a = a.clone();
    let mut b = b.clone();
    a.inode = 0;
    b.inode = 0;
    a == b
}
pub(crate) fn save_entries(
    entries: &mut [CpioEntry],
    format: CompressedFormat,
    cancel: &AtomicBool,
) -> Result<Vec<u8>> {
    cpio::sort(entries);
    cpio::assign_inodes(entries, false)?;
    let mut writer = CompressedWriter::new(Cursor::new(Vec::new()), format)?;
    cpio::save(&mut writer, entries, false, cancel)?;
    Ok(writer.finish()?.into_inner())
}
fn data<'a>(entries: &'a [CpioEntry], name: &[u8]) -> Result<&'a [u8]> {
    let e = entries
        .iter()
        .find(|e| e.path == name)
        .context("required ramdisk entry missing")?;
    if let CpioEntryData::Data(data) = &e.data {
        Ok(data)
    } else {
        bail!("ramdisk entry has no data")
    }
}
fn unxz(bytes: &[u8]) -> Result<Vec<u8>> {
    let reader = CompressedReader::new(Cursor::new(bytes), false)?;
    ensure!(
        reader.format() == CompressedFormat::Xz,
        "expected XZ payload"
    );
    let mut b = Vec::new();
    reader.take(8_388_609).read_to_end(&mut b)?;
    ensure!(b.len() <= 8_388_608, "XZ payload exceeds limit");
    Ok(b)
}
pub(crate) fn validate_patched_entries(
    entries: &[CpioEntry],
    old: &[CpioEntry],
    apk: &HashMap<String, Vec<u8>>,
    sha1: &str,
    preinit: &str,
) -> Result<()> {
    ensure!(
        data(entries, b"init")? == apk[PAYLOADS[0]],
        "patched init differs from pinned Magisk payload"
    );
    for (path, source) in [
        (b"overlay.d/sbin/magisk.xz".as_slice(), PAYLOADS[1]),
        (b"overlay.d/sbin/init-ld.xz", PAYLOADS[2]),
    ] {
        ensure!(
            unxz(data(entries, path)?)? == apk[source],
            "patched overlay payload mismatch"
        );
    }
    ensure!(
        unxz(data(entries, b".backup/init.xz")?)? == data(old, b"init")?,
        "original init backup mismatch"
    );
    let mut groups = std::collections::HashMap::<&[u8], Vec<&CpioEntry>>::new();
    for e in old {
        groups.entry(&e.path).or_default().push(e);
    }
    for (path, group) in groups.iter().filter(|(_, g)| g.len() > 1) {
        let preserved: Vec<_> = entries
            .iter()
            .filter(|e| e.path.as_slice() == *path)
            .collect();
        ensure!(
            preserved.len() == group.len()
                && preserved.iter().all(|e| same_empty_directory(group[0], e)),
            "repeated stock directory records changed or collapsed"
        );
    }
    let config = std::str::from_utf8(data(entries, b".backup/.magisk")?)?;
    for line in [
        format!("SHA1={sha1}"),
        "KEEPVERITY=true".into(),
        "KEEPFORCEENCRYPT=true".into(),
        "RECOVERYMODE=false".into(),
        "VENDORBOOT=false".into(),
    ] {
        ensure!(
            config.lines().filter(|l| *l == line).count() == 1,
            "Magisk config mismatch: {line}"
        );
    }
    if preinit.is_empty() {
        ensure!(
            !config.lines().any(|l| l.starts_with("PREINITDEVICE=")),
            "Unexpected pre-init override"
        );
    } else {
        ensure!(
            config
                .lines()
                .filter(|l| *l == format!("PREINITDEVICE={preinit}"))
                .count()
                == 1,
            "Pre-init override mismatch"
        );
    }
    Ok(())
}

#[cfg(test)]
mod cancel_tests {
    use super::*;
    #[test]
    fn cancellation_is_not_a_retryable_interruption() {
        let cancelled = AtomicBool::new(true);
        let mut input = ExactInput(Cursor::new(b"input".as_slice()), &cancelled);
        let mut buf = [0u8; 1];
        let error = input.read(&mut buf).unwrap_err();
        assert_ne!(
            error.kind(),
            std::io::ErrorKind::Interrupted,
            "read_to_end retries Interrupted forever"
        );
    }
}
