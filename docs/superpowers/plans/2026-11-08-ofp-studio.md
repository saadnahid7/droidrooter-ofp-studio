# OFP Studio Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. The user approved the design and explicitly waived further design questions.

**Goal:** Ship a portable, dependency-free native C Windows GUI OFP extractor and publish source and EXE.
**Architecture:** A worker-driven Win32 interface consumes a shared archive model and streaming extraction engine. CNG supplies cryptography; XMLLite parses Qualcomm metadata.
**Tech Stack:** C11, MinGW-w64, Win32, CNG, XMLLite, CMake, development-only Python fixtures.
**Spec:** ../specs/2026-11-08-ofp-studio-design.md

## Global Constraints
- Windows 10/11 x64 GUI; no Python, terminal or extra downloads required for users.
- No firmware or private workspace data in the public repository.
- No overwrite, path escape, unsupported-format success, or unverified speed claims.
- MTK and Qualcomm OFP; ZIP wrappers and OPS excluded.

## Review Focus
- Malformed metadata, integer overflow and malicious output paths must fail before writes.
- Cancellation must leave only completed files and remove owned partial files.
- Native dialogs, selection/filter state and progress must remain usable and responsive.
- Vendor hashes and generated manifests must not be conflated.
- Sparse images and missing references must prevent misleading compatibility scatters.

### Task 1: Archive inspection and native cryptography
Files: src/ofp.h, src/crypto.c, src/archive.c, src/mtk.c, src/qc.c, tests/driver.c, tests/test_engine.py.
Interfaces: ofp_inspect(path, archive, error), ofp_free(archive); archive has entries with 64-bit spans, selected flags, format, key and IV.
- [x] Add Python-generated OFP fixtures and a native driver; observe inspection failures before decoding exists.
- [x] Implement CNG hashing/CFB128, MTK footer/table/key detection and Qualcomm XML metadata parsing.
- [x] Test known key profiles, both Qualcomm page sizes, unknown keys, truncation, traversal and conflicting duplicates.
- [x] Commit with green tests.

### Task 2: Streaming extraction, manifests and scatter option
Files: src/extract.c, src/scatter.c, tests/test_engine.py.
Interfaces: ofp_extract(archive, destination, options, callback, context, cancel, error); callback reports bytes/file/status.
- [x] Add failing extraction tests for content parity, selective output, collisions, hash mismatch, cancellation and sparse validation.
- [x] Implement bounded streaming, exclusive partial files, SHA-256, optional reread verification, path/reparse safety, vendor hash validation and separate compatibility scatter.
- [x] Run full suite and commit.

### Task 3: Native graphical interface
Files: src/gui.c, assets/app.rc, assets/app.manifest, CMakeLists.txt, build.ps1.
- [x] Build GUI with file and folder browsers, checkbox/filter list, details, selection totals, options, progress/cancel, logs and result actions.
- [x] Exercise actual app input selection, inspection, selected extraction, cancellation and dialogs. Validate UI at supported minimum size and capture screenshot.
- [x] Run tests and commit.

### Task 4: Real firmware qualification and final review
- [x] Extract full local F15 OFP using native engine; compare every SHA-256 with the prior verified Python extraction.
- [x] Measure time/memory with equivalent options; report disk/cache limits.
- [x] Request fresh whole-branch review; fix material findings with regression coverage.
- [x] Confirm EXE imports only system DLLs; run release build tests.

### Task 5: Public distribution
Files: README.md, LICENSE, THIRD_PARTY_NOTICES.md, docs/qualification.md, .github/workflows/build.yml.
- [ ] Package portable EXE, notices and instructions; generate checksums.
- [ ] Review staged files and secret/private-data exclusions, commit, create public saadnahid7/ofp-studio, push and publish tagged release with EXE/ZIP/checksums.
- [ ] Verify remote repository, release assets and CI results.
