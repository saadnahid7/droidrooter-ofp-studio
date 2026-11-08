# Implementation ledger — 2026-11-08-ofp-studio

Design approved, further design questions explicitly waived. New repository isolated from firmware at the isolated source folder, branch build/native-gui. No pre-existing source/tests.
Pre-flight: GUI and test driver consume the same archive/extraction interface; inspection owns metadata and extraction never changes source spans.
Ruling: Native inline implementation, one independent final reviewer; no separate worktree needed for a brand-new isolated repository.
Tasks 1-2 core: 14 behavioral tests passed after observed RED failures; cryptography matches independently encrypted Python fixtures, including non-block-aligned CFB128 and unencrypted spans. Scatter eligibility/conversion remains next.

Tasks 1-3: implemented native MTK/QC inspection, bounded extraction, verification, safe paths, scatter conversion and GUI. 21 behavioral tests pass. RED-to-GREEN recorded for decoding/extraction and file/directory conflicts.
Real firmware qualification: 60 logical / 56 unique F15 entries; all 56 native hashes match the verified Python reference. Full native run with readback and scatter: 334.0 seconds.
GUI: file/folder dialogs, inspection, clearing selection, single-file extraction, manifest and readback verified through the actual app. Dark-theme checkbox contrast corrected. Branding updated per user steering to DroidRooter's OFP Studio with native {r}/extraction-arrow logo.
Ruling: local-drive destinations only; reject UNC/reparse paths in v1 to keep directory ownership checks reliable. Documented in app and README.

Final review: one independent read-only review completed, independently reran the initial 21 tests. All eight Important findings fixed: same-handle output finalization and deletion; QC protected spans and nesting; strict scatter/sparse checks; output namespace reservation; cancellation and artifact rollback; retained duplicate labels; truthful verification status; modal close race. Regression suite expanded to 29 passing tests.
Ruling: report metadata is a separate OFP-Studio-report.txt alongside standard SHA256SUMS.txt, preserving interoperable checksums and all logical labels.
Review polish: free-space display now queries the nearest existing parent; inspected package size is displayed.
Benchmark: native 231.401 s / 9,359,360-byte peak working set; Python 224.670 s / 11,325,440 bytes. Approximately 17.4% less peak memory; no speedup claim.

Final release F15 recheck: 313.7 seconds; all 56 hashes match reference; logical aliases and validated scatter present; no partial files. GUI cancellation independently rehearsed with readback enabled; status correctly says verification did not complete.
Tasks 1-4 complete. Final reviewed fixes covered by 29 green behavioral tests. No deferred minor findings remain.

Task 5 complete: public repository and v1.0.0 release published with standalone EXE, portable ZIP and SHA-256 checksums. GitHub Windows build and all 29 tests passed independently. Uploaded GitHub asset digests match local files; ZIP contents and EXE identity verified. Packaged EXE launched successfully. CI: https://github.com/saadnahid7/droidrooter-ofp-studio/actions/runs/42476163646
