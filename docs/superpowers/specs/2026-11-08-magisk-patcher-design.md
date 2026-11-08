# Integrated Magisk boot-image patcher

The user requested an integrated patcher using PixelRootStation as a reference. Their prior instruction to proceed without further design questions remains in force. Keep the native C GUI and a single portable EXE with no user-installed runtime or helper tools.

## Architecture and scope

Link a Rust static library into the C Windows application, reusing the pinned GPL-3.0-only avbroot MagiskRootPatcher at revision 88362620a21377b160ae7500f64f144a2262a281. PixelRootStation's adapter informs strict ramdisk, payload, checksum and AVB validation, but its Pixel-8-only device qualification requirements are not copied into this general file-preparation tool. The integrated distribution becomes GPL-3.0-only; retain the original MIT notices and all upstream notices, and distribute corresponding source.

Embed the official Magisk 31.1 APK as a resource, pinned to SHA-256 89cefac0a318afa39fb3880c45c87b036c05bc1ebf02a7a715b7f8d7c26ac6e7 (also verified against the official GitHub release digest). No runtime downloads, Android shell, ADB, phone flashing or external helper executable. Build-time tools and dependencies may be downloaded by developers/CI; the user EXE remains self-contained.

Support ARM64 Magisk payloads and standard Android boot header versions 0-4, including legacy F15 v2 boot and kernel-less v4 init_boot. Reject vendor_boot, unsupported signatures/wrappers, already-patched ramdisks, malformed or ambiguous images, oversized data and insufficient partition space. For appended AVB, validate original header/hash, recompute output hash metadata, preserve rollback/flags and explicitly remove the invalidated vendor signature. Recompute the v0-v2 boot image ID from the modified components. Preserve kernel, DTB, recovery DTBO, second stage and unrelated header fields.

## GUI

Add a sidebar action opening a dedicated Magisk patcher window. Offer Browse stock image, Use selected extracted image, output folder picker with generated filenames, detected header/version/size, bundled Magisk version/architecture, an optional pre-init override under Advanced, Patch image, Cancel, Open output folder and a status/log view. The default omits PREINITDEVICE using official offline behavior. An override is user-supplied and never guessed from an OFP; do not claim it was device-observed. Keep verity and force-encryption settings enabled in the initial release; no recovery installation or kernel hex-patching switches that the implementation cannot validate.

Use a worker thread and cancellation token. The patcher consumes a snapshot of the image and pinned APK, validates and reparses the result, writes a new file plus provenance report without overwriting the original or existing outputs, and uses the same owned-handle publication/cleanup rules as extraction. Cancellation removes owned incomplete/generated artifacts. Display file preparation success separately from phone bootability: no actual phone is flashed or physically qualified by this feature.

## Validation and release

Synthetic v0/v2/v4 images, gzip/legacy-LZ4 ramdisks, original SHA1/config, payload parity, backup preservation, legacy ID, appended AVB, bounds, already-patched rejection and cancellation need coverage. Exercise the actual GUI. Patch the real F15 boot image offline and verify image/ramdisk/kernel/hash invariants; this establishes file transformation only. Rerun all extraction regressions. Publish an updated public source tree and v1.1.0 release with the EXE, ZIP, checksums and complete corresponding-source bundle.
