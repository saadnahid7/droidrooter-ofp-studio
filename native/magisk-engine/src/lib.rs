// SPDX-License-Identifier: GPL-3.0-only
use anyhow::{Context, Result, bail, ensure};
use avbroot::{
    format::{
        avb::{self, AlgorithmType},
        bootimage::{BootImage, BootImageExt},
        cpio::CpioEntryData,
    },
    patch::boot::{BootImagePatch, BootLayoutInfo, MagiskRootPatcher},
    stream::{FromReader, ToWriter},
};
use sha1::{Digest, Sha1};
use sha2::Sha256;
use std::{
    io::{Cursor, Read, Seek, SeekFrom, Write},
    path::Path,
    sync::atomic::{AtomicBool, Ordering},
};
mod ffi;
mod support;
pub struct Patched {
    pub bytes: Vec<u8>,
    pub report: String,
}
pub const APK_SHA256: &str = "89cefac0a318afa39fb3880c45c87b036c05bc1ebf02a7a715b7f8d7c26ac6e7";
fn check_cancel(c: &AtomicBool) -> Result<()> {
    ensure!(!c.load(Ordering::Relaxed), "Cancelled");
    Ok(())
}
fn ramdisk(b: &BootImage) -> Result<&[u8]> {
    match b {
        BootImage::V0Through2(b) => Ok(&b.ramdisk),
        BootImage::V3Through4(b) => Ok(&b.ramdisk),
        _ => bail!("vendor_boot is not a supported patch target"),
    }
}
fn set_ramdisk(b: &mut BootImage, r: Vec<u8>) {
    match b {
        BootImage::V0Through2(b) => b.ramdisk = r,
        BootImage::V3Through4(b) => b.ramdisk = r,
        _ => unreachable!(),
    }
}
fn target_hash(header: &avb::Header, target: &str) -> Result<usize> {
    let matches: Vec<_> = header
        .descriptors
        .iter()
        .enumerate()
        .filter_map(|(i, d)| match d {
            avb::Descriptor::Hash(d) if d.partition_name == target => Some(i),
            _ => None,
        })
        .collect();
    ensure!(
        matches.len() == 1,
        "Exactly one AVB hash descriptor must match the selected boot partition"
    );
    Ok(matches[0])
}
fn append_avb(
    payload: &[u8],
    header: &avb::Header,
    footer: &mut avb::Footer,
    size: usize,
) -> Result<Vec<u8>> {
    let mut serialized = Cursor::new(Vec::new());
    header.to_writer(&mut serialized)?;
    let metadata = serialized.into_inner();
    let at = payload.len().checked_add(4095).context("AVB overflow")? & !4095;
    ensure!(
        at.checked_add(metadata.len())
            .and_then(|n| n.checked_add(4096))
            .is_some_and(|end| end <= size),
        "Patched image does not leave room for AVB metadata"
    );
    let mut output = vec![0; size];
    output[..payload.len()].copy_from_slice(payload);
    output[at..at + metadata.len()].copy_from_slice(&metadata);
    footer.original_image_size = payload.len() as u64;
    footer.vbmeta_offset = at as u64;
    footer.vbmeta_size = metadata.len() as u64;
    let mut w = Cursor::new(&mut output);
    w.seek(SeekFrom::Start((size - 64) as u64))?;
    footer.to_writer(&mut w)?;
    Ok(output)
}
fn legacy_id(b: &mut BootImage) {
    if let BootImage::V0Through2(b) = b {
        let mut h = Sha1::new();
        let mut add = |bytes: &[u8]| {
            h.update(bytes);
            h.update((bytes.len() as u32).to_le_bytes());
        };
        add(&b.kernel);
        add(&b.ramdisk);
        add(&b.second);
        if let Some(v) = &b.v1_extra {
            add(&v.recovery_dtbo);
        }
        if let Some(v) = &b.v2_extra {
            add(&v.dtb);
        }
        let bytes = h.finalize();
        b.id = [0; 8];
        for (i, chunk) in bytes.chunks_exact(4).enumerate() {
            b.id[i] = u32::from_le_bytes(chunk.try_into().unwrap());
        }
    }
}
pub fn patch(stock: &[u8], apk: &Path, preinit: &str, cancel: &AtomicBool) -> Result<Patched> {
    check_cancel(cancel)?;
    ensure!(
        stock.len() >= 4096 && stock.len() <= 134_217_728 && stock.get(..8) == Some(b"ANDROID!"),
        "Select a standard Android boot.img or init_boot.img (maximum 128 MiB)"
    );
    ensure!(
        preinit.len() <= 128
            && preinit
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b"._-".contains(&b)),
        "Invalid optional pre-init partition override"
    );
    let meta = std::fs::metadata(apk)?;
    ensure!(meta.len() == 10_679_283, "Bundled Magisk APK size mismatch");
    let apk_bytes = std::fs::read(apk)?;
    ensure!(
        hex::encode(Sha256::digest(&apk_bytes)) == APK_SHA256,
        "Bundled Magisk APK checksum mismatch"
    );
    let mut snapshot = tempfile::NamedTempFile::new()?;
    snapshot.write_all(&apk_bytes)?;
    snapshot.flush()?;
    let payloads = support::read_payloads(snapshot.path())?;
    let mut image =
        BootImage::from_reader(Cursor::new(stock)).context("Parse Android boot image")?;
    if let BootImage::V3Through4(b) = &image {
        ensure!(
            b.v4_extra.as_ref().is_none_or(|v| v.signature.is_none()),
            "Embedded boot signature is not supported; refusing to preserve an invalidated signature"
        );
    }
    let original = image.clone();
    let version = image.header_version();
    let original_ramdisk = ramdisk(&image)?.to_vec();
    let kernel = match &image {
        BootImage::V0Through2(b) => &b.kernel,
        BootImage::V3Through4(b) => &b.kernel,
        _ => unreachable!(),
    };
    let partition = if kernel.is_empty() {
        "init_boot"
    } else {
        "boot"
    };
    if !kernel.is_empty() {
        let mut reader =
            avbroot::format::compression::CompressedReader::new(Cursor::new(kernel), true)?;
        let mut prefix = [0u8; 64];
        reader.read_exact(&mut prefix)?;
        ensure!(
            &prefix[56..60] == b"ARM\x64",
            "Only ARM64 Linux boot images are supported by the bundled Magisk payload"
        );
    }
    ensure!(
        !original_ramdisk.is_empty(),
        "Image has no ramdisk; choose the correct boot/init_boot image"
    );
    let (old_entries, _) = support::load_entries(&original_ramdisk, cancel)?;
    ensure!(
        old_entries.iter().any(|e| e.path == b"init"),
        "Stock ramdisk has no init"
    );
    ensure!(
        !old_entries
            .iter()
            .any(|e| e.path.starts_with(b".backup") || e.path == b"overlay.d/sbin/magisk.xz"),
        "Image is already patched or contains an unknown backup state"
    );
    let mut canonical = Cursor::new(Vec::new());
    image.to_writer(&mut canonical)?;
    let canonical = canonical.into_inner();
    ensure!(
        canonical.len() <= stock.len() && stock[..canonical.len()] == canonical,
        "Noncanonical boot fields/padding are not supported"
    );
    let has_avb = stock.get(stock.len() - 64..stock.len() - 60) == Some(b"AVBf");
    let mut avb_input = if has_avb {
        Some(avb::load_image(Cursor::new(stock))?)
    } else {
        None
    };
    let mut input_signature = false;
    if let Some((header, footer, total)) = &avb_input {
        let footer = footer.as_ref().context("Missing appended AVB footer")?;
        ensure!(*total == stock.len() as u64, "AVB image size mismatch");
        let mut encoded = Cursor::new(Vec::new());
        header.to_writer(&mut encoded)?;
        let encoded = encoded.into_inner();
        let at = usize::try_from(footer.vbmeta_offset)?;
        ensure!(
            encoded.len() as u64 == footer.vbmeta_size
                && stock.get(at..at.checked_add(encoded.len()).context("AVB overflow")?)
                    == Some(encoded.as_slice()),
            "Noncanonical AVB metadata"
        );
        input_signature = header.verify()?.is_some();
        let index = target_hash(header, partition)?;
        let avb::Descriptor::Hash(d) = &header.descriptors[index] else {
            unreachable!()
        };
        ensure!(
            matches!(d.partition_name.as_str(), "boot" | "init_boot")
                && d.hash_algorithm == "sha256"
                && d.image_size == footer.original_image_size,
            "Unsupported AVB partition/hash metadata"
        );
        d.verify(Cursor::new(stock), cancel)?;
        let end = usize::try_from(d.image_size)?;
        ensure!(
            canonical.len() <= end
                && end <= at
                && stock[canonical.len()..at].iter().all(|b| *b == 0)
                && stock[at + encoded.len()..stock.len() - 64]
                    .iter()
                    .all(|b| *b == 0),
            "Unrecognized boot image tail"
        );
    } else {
        ensure!(
            stock[canonical.len()..].iter().all(|b| *b == 0),
            "Unrecognized data/signature after bare boot image"
        );
    }
    check_cancel(cancel)?;
    MagiskRootPatcher::new(snapshot.path(), Some(preinit), None, false)?.patch(
        BootLayoutInfo {
            has_recovery: false,
        },
        &mut image,
        cancel,
    )?;
    let sha1 = hex::encode(Sha1::digest(stock));
    let (mut entries, format) = support::load_entries(ramdisk(&image)?, cancel)?;
    let cfg = entries
        .iter_mut()
        .find(|e| e.path == b".backup/.magisk")
        .context("Magisk backup configuration is missing")?;
    let CpioEntryData::Data(data) = &mut cfg.data else {
        bail!("Invalid Magisk configuration")
    };
    let text = std::str::from_utf8(data)?;
    ensure!(
        text.lines().filter(|l| l.starts_with("SHA1=")).count() == 1,
        "Ambiguous original-image SHA1"
    );
    *data = (text
        .lines()
        // Official boot_patch.sh omits PREINITDEVICE when unavailable offline.
        // avbroot requires Some for this version; remove its empty config entry.
        .filter(|line| !(preinit.is_empty() && line.starts_with("PREINITDEVICE=")))
        .map(|line| {
            if line.starts_with("SHA1=") {
                format!("SHA1={sha1}")
            } else {
                line.to_owned()
            }
        })
        .collect::<Vec<_>>()
        .join("\n")
        + "\n")
        .into_bytes();
    support::validate_patched_entries(&entries, &old_entries, &payloads, &sha1, preinit)?;
    set_ramdisk(
        &mut image,
        support::save_entries(&mut entries, format, cancel)?,
    );
    legacy_id(&mut image);
    let mut probe = image.clone();
    set_ramdisk(&mut probe, original_ramdisk);
    if let (BootImage::V0Through2(p), BootImage::V0Through2(o)) = (&mut probe, &original) {
        p.id = o.id;
    }
    ensure!(probe == original, "Non-ramdisk boot components changed");
    let mut output = Cursor::new(Vec::new());
    image.to_writer(&mut output)?;
    let logical = output.position();
    let avb_policy = if let Some((header, footer, _)) = &mut avb_input {
        let index = target_hash(header, partition)?;
        let avb::Descriptor::Hash(d) = &mut header.descriptors[index] else {
            unreachable!()
        };
        d.image_size = logical;
        d.update(Cursor::new(output.get_ref()), cancel)?;
        header.clear_sig();
        header.algorithm_type = AlgorithmType::None;
        let b = append_avb(
            output.get_ref(),
            header,
            footer.as_mut().unwrap(),
            stock.len(),
        )?;
        output = Cursor::new(b);
        let (h, _, _) = avb::load_image(Cursor::new(output.get_ref()))?;
        ensure!(
            h.verify()?.is_none(),
            "Output must not retain a vendor signature"
        );
        let index = target_hash(&h, partition)?;
        let avb::Descriptor::Hash(d) = &h.descriptors[index] else {
            unreachable!()
        };
        d.verify(Cursor::new(output.get_ref()), cancel)?;
        "hash_recomputed_unsigned_unlocked_device_only"
    } else {
        "bare_image_no_avb_footer"
    };
    let mut bytes = output.into_inner();
    ensure!(
        bytes.len() <= stock.len(),
        "Patched image exceeds the original partition-image size"
    );
    bytes.resize(stock.len(), 0);
    let parsed = BootImage::from_reader(Cursor::new(&bytes))?;
    let (entries, _) = support::load_entries(ramdisk(&parsed)?, cancel)?;
    support::validate_patched_entries(&entries, &old_entries, &payloads, &sha1, preinit)?;
    check_cancel(cancel)?;
    let report = serde_json::to_string_pretty(
        &serde_json::json!({"schema":1,"tool":"DroidRooter's OFP Studio","magisk_version":"31.1","magisk_version_code":31100,"architecture":"arm64-v8a","stock_sha256":hex::encode(Sha256::digest(stock)),"original_sha1":sha1,"magisk_sha256":APK_SHA256,"output_sha256":hex::encode(Sha256::digest(&bytes)),"header_version":version,"image_size":bytes.len(),"preinit_device":preinit,"preinit_evidence":if preinit.is_empty() {"omitted_official_offline_mode"} else {"user_supplied_not_device_verified"},"keep_verity":true,"keep_force_encrypt":true,"avb_policy":avb_policy,"input_avb_signature_verified_against_embedded_key":input_signature,"physically_qualified":false,"flash_performed":false,"upstream_revision":"88362620a21377b160ae7500f64f144a2262a281"}),
    )?;
    Ok(Patched { bytes, report })
}
