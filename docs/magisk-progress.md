# Magisk integration ledger

Feature branch feature/magisk-patcher; existing extractor release preserved. Reference inspected read-only: PixelRootStation's Rust adapter is intentionally Pixel-8-v4-only; the general app must support F15 v2 independently.
Ruling: retain C GUI and extractor, link the GPL native Rust patch engine statically, embed verified official Magisk 31 APK. This preserves a single EXE but expands build tooling and changes the combined distribution license to GPL-3.0-only.
Official APK digest verified against GitHub release metadata and local bytes; no reference project's private/device metadata is copied.


User selected Magisk 31.1. Updated official APK hash, avbroot 3.37.1 and payload validation (31.1 removes stub.apk). Verified 29 extractor, eight C workflow and native fixture tests. Fixed cancellation retry loop and preserved multiple F15 AVB descriptors. Independent review found output suggestion overflow; removed filename append while implementing user-requested folder output. Default now omits pre-init override per official offline boot_patch.sh; optional override is hidden under Advanced. No device-derived facts are guessed.
