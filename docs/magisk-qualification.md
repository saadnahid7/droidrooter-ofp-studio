# Magisk 31.1 qualification

The Windows x64 application embeds the official v31.1 APK (version code 31100),
SHA-256 `89cefac0a318afa39fb3880c45c87b036c05bc1ebf02a7a715b7f8d7c26ac6e7`.
The patch engine uses avbroot 3.37.1, revision
`88362620a21377b160ae7500f64f144a2262a281`, through a statically linked Rust adapter.

Local behavioral checks: 29 extractor tests, eight C patch-workflow tests, one Rust
cancellation regression test and eight synthetic native patch tests pass. An
additional ignored/local-only test patches the actual F15 CPH2001 C.53 stock v2
boot image and validates the rebuilt payload and AVB hash. Firmware is not distributed.
Synthetic coverage includes boot v0/v4, payload/version validation, original init
backup, duplicate/unsafe CPIO entries, trailing compressed data, incorrect inputs
and cancellation. Header v1/v3 support follows the same parser families but has no
separate real-device fixture qualification.

The C workflow tests exercise the same code linked into the GUI: original file
preservation, readback, existing image/report protection, Unicode output paths,
invalid pre-init input and cancellation before patching and during output writing.
The native cancellation regression catches a read loop retrying Interrupted errors.

The real F15 has three AVB hash descriptors. The adapter selects the exact boot
descriptor, verifies it and the source header signature using the embedded key,
then retains other descriptors while replacing the boot hash and clearing the
modified header signature. Embedded-key verification is not independent vendor
authentication. Legacy boot ID SHA-1 is recalculated after ramdisk modification.

All testing is offline. A successful patch proves a structurally validated image
transformation, not bootability, root access, correct pre-init selection, a phone's
unlock state or acceptance by SP Flash Tool. Test pre-init values are fixtures,
not device facts. The application does not flash or connect to a phone.

The default omits PREINITDEVICE exactly as official boot_patch.sh does when it is
unavailable offline. Advanced permits an explicit override. No automatic live
device discovery is claimed. Output selection uses a folder picker and generated
filenames; final creation still rejects races/overwrites.

Packaged EXE GUI rehearsal: default patch with no pre-init override succeeded;
a second patch generated a numbered filename and preserved the first image/report.
The final progress bar reached completion. Runtime imports are Windows system DLLs only.

Additional release checks: the packaged GUI rejected an already-patched image and
cancelled a real-size F15 patch without publishing a new image/report or leaving
partial files. The complete corresponding-source ZIP passed CRC/duplicate/path
checks, resolved dependencies offline, built successfully with Cargo network
access disabled, and its rebuilt drivers passed all 29 extractor and 8 workflow
tests. An independent stdlib Python parser confirmed the F15 kernel/DTB, compressed
init backup, official APK payloads, legacy boot ID and AVB hashes/descriptors.
