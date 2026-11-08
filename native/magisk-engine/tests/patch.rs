use avbroot::{
    format::{
        bootimage::{BootImage, BootImageV0Through2, BootImageV3Through4, V4Extra},
        compression::{CompressedFormat, CompressedReader, CompressedWriter},
        cpio::{self, CpioEntry, CpioEntryData},
    },
    stream::{FromReader, ToWriter},
};
use ofp_magisk_engine::patch;
use sha1::{Digest, Sha1};
use std::{io::Cursor, path::PathBuf, sync::atomic::AtomicBool};
fn apk() -> PathBuf {
    PathBuf::from(
        std::env::var("OFP_MAGISK_APK").expect("Set OFP_MAGISK_APK to official Magisk 31.1 APK"),
    )
}
fn kernel() -> Vec<u8> {
    let mut k = vec![0; 64];
    k[56..60].copy_from_slice(b"ARM\x64");
    k
}
fn fixture(v4: bool) -> Vec<u8> {
    let c = AtomicBool::new(false);
    let mut entries = vec![
        CpioEntry::new_file(
            b"init",
            0o750,
            CpioEntryData::Data(b"original-init".to_vec()),
        ),
        CpioEntry::new_file(
            b"fstab.test",
            0o644,
            CpioEntryData::Data(b"preserve-fstab".to_vec()),
        ),
    ];
    let mut writer =
        CompressedWriter::new(Cursor::new(Vec::new()), CompressedFormat::Gzip).unwrap();
    cpio::save(&mut writer, &mut entries, false, &c).unwrap();
    let ramdisk = writer.finish().unwrap().into_inner();
    let image = if v4 {
        BootImage::V3Through4(BootImageV3Through4 {
            os_version: 0,
            reserved: [0; 4],
            cmdline: String::new(),
            v4_extra: Some(V4Extra { signature: None }),
            kernel: vec![],
            ramdisk,
        })
    } else {
        BootImage::V0Through2(BootImageV0Through2 {
            kernel_addr: 0x10008000,
            ramdisk_addr: 0x11000000,
            second_addr: 0,
            tags_addr: 0,
            page_size: 2048,
            os_version: 0,
            name: "fixture".into(),
            cmdline: "test=1".into(),
            id: [0; 8],
            extra_cmdline: String::new(),
            kernel: kernel(),
            ramdisk,
            second: vec![],
            v1_extra: None,
            v2_extra: None,
        })
    };
    let mut out = Cursor::new(Vec::new());
    image.to_writer(&mut out).unwrap();
    let mut b = out.into_inner();
    b.resize(4 * 1024 * 1024, 0);
    b
}
fn entries(image: &BootImage) -> Vec<CpioEntry> {
    let r = match image {
        BootImage::V0Through2(b) => &b.ramdisk,
        BootImage::V3Through4(b) => &b.ramdisk,
        _ => panic!(),
    };
    let mut reader = CompressedReader::new(Cursor::new(r), false).unwrap();
    cpio::load(&mut reader, false, &AtomicBool::new(false)).unwrap()
}
#[test]
fn patches_legacy_and_init_boot_with_original_sha1() {
    for v4 in [false, true] {
        let stock = fixture(v4);
        let result = patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).unwrap();
        assert_eq!(result.bytes.len(), stock.len());
        let image = BootImage::from_reader(Cursor::new(&result.bytes)).unwrap();
        let e = entries(&image);
        let cfg = e.iter().find(|e| e.path == b".backup/.magisk").unwrap();
        let CpioEntryData::Data(data) = &cfg.data else {
            panic!()
        };
        let text = std::str::from_utf8(data).unwrap();
        assert!(text.contains(&format!("SHA1={}\n", hex::encode(Sha1::digest(&stock)))));
        assert!(text.contains("PREINITDEVICE=metadata\n"));
        assert!(text.contains("KEEPVERITY=true\nKEEPFORCEENCRYPT=true\n"));
        if let BootImage::V0Through2(b) = image {
            assert_eq!(b.kernel, kernel());
            assert_ne!(b.id, [0; 8]);
        }
        assert!(result.report.contains("physically_qualified"));
    }
}
#[test]
fn rejects_already_patched_and_invalid_preinit() {
    let stock = fixture(true);
    assert!(patch(&stock, &apk(), "../bad", &AtomicBool::new(false)).is_err());
    let first = patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).unwrap();
    assert!(patch(&first.bytes, &apk(), "metadata", &AtomicBool::new(false)).is_err());
}
#[test]
fn rejects_malformed_and_honors_cancel() {
    assert!(patch(b"bad", &apk(), "metadata", &AtomicBool::new(false)).is_err());
    assert!(patch(&fixture(false), &apk(), "metadata", &AtomicBool::new(true)).is_err());
}
#[test]
fn rejects_unknown_kernel_architecture() {
    let mut image = BootImage::from_reader(Cursor::new(fixture(false))).unwrap();
    let BootImage::V0Through2(b) = &mut image else {
        panic!()
    };
    b.kernel = vec![0; 64];
    let mut out = Cursor::new(Vec::new());
    image.to_writer(&mut out).unwrap();
    let mut bytes = out.into_inner();
    bytes.resize(4 * 1024 * 1024, 0);
    assert!(patch(&bytes, &apk(), "metadata", &AtomicBool::new(false)).is_err());
}
#[test]
#[ignore = "Requires local F15 firmware; not redistributed"]
fn real_f15_offline_transformation_if_supplied() {
    if let Ok(path) = std::env::var("OFP_F15_STOCK") {
        let stock = std::fs::read(path).unwrap();
        let result = patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).unwrap();
        assert_eq!(result.bytes.len(), stock.len());
        if let Ok(out) = std::env::var("OFP_F15_PATCHED") {
            std::fs::write(out, &result.bytes).unwrap();
        }
        println!("{}", result.report);
    }
}
fn appended() -> Vec<u8> {
    use avbroot::format::avb::{AlgorithmType, Descriptor, Footer, HashDescriptor, Header};
    let stock = fixture(false);
    let image = BootImage::from_reader(Cursor::new(&stock)).unwrap();
    let mut logical = Cursor::new(Vec::new());
    image.to_writer(&mut logical).unwrap();
    let mut d = HashDescriptor {
        image_size: logical.position(),
        hash_algorithm: "sha256".into(),
        partition_name: "boot".into(),
        salt: b"fixture".to_vec(),
        root_digest: vec![],
        flags: 0,
        reserved: [0; 60],
    };
    d.update(Cursor::new(logical.get_ref()), &AtomicBool::new(false))
        .unwrap();
    let mut other = d.clone();
    other.partition_name = "recovery".into();
    other.root_digest = vec![0xab; 32];
    let header = Header {
        required_libavb_version_major: 1,
        required_libavb_version_minor: 0,
        algorithm_type: AlgorithmType::None,
        hash: vec![],
        signature: vec![],
        public_key: vec![],
        public_key_metadata: vec![],
        descriptors: vec![Descriptor::Hash(d), Descriptor::Hash(other)],
        rollback_index: 123,
        flags: 2,
        rollback_index_location: 4,
        release_string: "test".into(),
        reserved: [0; 80],
    };
    let mut meta = Cursor::new(Vec::new());
    header.to_writer(&mut meta).unwrap();
    let meta = meta.into_inner();
    let at = (logical.get_ref().len() + 4095) & !4095;
    let mut bytes = vec![0; stock.len()];
    bytes[..logical.get_ref().len()].copy_from_slice(logical.get_ref());
    bytes[at..at + meta.len()].copy_from_slice(&meta);
    let footer = Footer {
        version_major: 1,
        version_minor: 0,
        original_image_size: logical.position(),
        vbmeta_offset: at as u64,
        vbmeta_size: meta.len() as u64,
        reserved: [0; 28],
    };
    let end = bytes.len() - 64;
    footer.to_writer(Cursor::new(&mut bytes[end..])).unwrap();
    bytes
}
#[test]
fn preserves_other_avb_partitions_and_rollback_fields() {
    use avbroot::format::avb::{self, Descriptor};
    let stock = appended();
    let result = patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).unwrap();
    let (h, _, _) = avb::load_image(Cursor::new(&result.bytes)).unwrap();
    assert_eq!(h.rollback_index, 123);
    assert_eq!(h.flags, 2);
    assert_eq!(h.rollback_index_location, 4);
    assert!(h.verify().unwrap().is_none());
    assert_eq!(h.descriptors.len(), 2);
    for d in h.descriptors {
        if let Descriptor::Hash(d) = d {
            if d.partition_name == "recovery" {
                assert_eq!(d.root_digest, vec![0xab; 32]);
            } else {
                d.verify(Cursor::new(&result.bytes), &AtomicBool::new(false))
                    .unwrap();
            }
        }
    }
}
#[test]
fn rejects_corrupt_avb_hash_and_unknown_tail() {
    let mut stock = appended();
    stock[2048] ^= 1;
    assert!(patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).is_err());
    let mut stock = fixture(true);
    let end = stock.len() - 1;
    stock[end] = 1;
    assert!(patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).is_err());
}
#[test]
fn rejects_too_small_partition_and_wrong_apk() {
    let stock = fixture(true);
    assert!(patch(&stock[..8192], &apk(), "metadata", &AtomicBool::new(false)).is_err());
    let temp = tempfile::NamedTempFile::new().unwrap();
    assert!(patch(&stock, temp.path(), "metadata", &AtomicBool::new(false)).is_err());
}
#[test]
fn supports_legacy_lz4_and_rejects_concatenated_gzip() {
    use std::io::{Read, Write};
    let original = fixture(true);
    let mut image = BootImage::from_reader(Cursor::new(&original)).unwrap();
    let BootImage::V3Through4(b) = &mut image else {
        panic!()
    };
    let mut raw = Vec::new();
    CompressedReader::new(Cursor::new(&b.ramdisk), false)
        .unwrap()
        .read_to_end(&mut raw)
        .unwrap();
    let mut writer =
        CompressedWriter::new(Cursor::new(Vec::new()), CompressedFormat::Lz4Legacy).unwrap();
    writer.write_all(&raw).unwrap();
    b.ramdisk = writer.finish().unwrap().into_inner();
    let mut out = Cursor::new(Vec::new());
    image.to_writer(&mut out).unwrap();
    let mut stock = out.into_inner();
    stock.resize(original.len(), 0);
    assert!(patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).is_ok());
    let mut image = BootImage::from_reader(Cursor::new(&original)).unwrap();
    let BootImage::V3Through4(b) = &mut image else {
        panic!()
    };
    let extra = b.ramdisk.clone();
    b.ramdisk.extend(extra);
    let mut out = Cursor::new(Vec::new());
    image.to_writer(&mut out).unwrap();
    let mut stock = out.into_inner();
    stock.resize(original.len(), 0);
    assert!(patch(&stock, &apk(), "metadata", &AtomicBool::new(false)).is_err());
}
