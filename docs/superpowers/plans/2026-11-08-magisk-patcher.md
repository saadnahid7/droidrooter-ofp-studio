# Magisk Patcher Implementation Plan

> Execute inline under executing-plans and test-driven-development. The user waived additional design questions. One independent final review before release.

**Goal:** Integrate offline Magisk image patching into the portable GUI and publish v1.1.0.
**Architecture:** C GUI/owned output I/O + statically linked Rust format/patching engine + embedded pinned Magisk APK.
**Spec:** ../specs/2026-11-08-magisk-patcher-design.md

## Constraints and review focus
- No user-side runtime/tools, subprocess patcher, phone flashing or guessed pre-init device.
- Pin and validate Magisk payload, preserve original image and non-ramdisk components.
- Reject malformed/ambiguous layouts and already-patched images; never retain invalid vendor signatures.
- Owned-handle publication, input/output collision handling and cooperative cancellation.
- GPL corresponding source and notices accompany the combined distribution.

### Task 1: Native patch engine
- [ ] Add failing transformation tests using actual pinned Magisk APK and synthetic image fixtures.
- [ ] Implement bounded image/ramdisk validation, Magisk patching, original SHA1 correction, legacy boot ID and appended AVB handling.
- [ ] Expose a narrow C ABI with cancellation and owned buffers; run engine tests.

### Task 2: C integration and GUI
- [ ] Link the engine and embed the APK; retain system-DLL-only runtime imports.
- [ ] Add patcher window and worker, image/output folder browser, extracted-image shortcut and optional Advanced override.
- [ ] Publish image/provenance safely and test GUI success, errors and cancellation.

### Task 3: Qualification and distribution
- [ ] Verify real F15 offline output and all extraction regressions.
- [ ] Independent code review; fix material findings and rerun checks.
- [ ] Update licenses/docs/CI, package EXE and corresponding source, publish v1.1.0 and verify assets/CI.
