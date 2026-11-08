# OFP Studio design

Status: approved. The user approved implementation, testing and public GitHub publication, waived further design questions, and subsequently requested DroidRooter branding and removal of promotional interface copy.

## Intent and requirements

Build an easy-to-use, attractive, open-source OFP firmware extractor in C. The user requires a fully graphical Windows application that uses built-in Windows components and needs no separately downloaded runtime or tools. Publish source and a portable executable in a new public repository under the authenticated account, saadnahid7. Repository name: droidrooter-ofp-studio. Product name: DroidRooter's OFP Studio.

The immediate real-world reference is the CPH2001/F15 C.53 MediaTek OFP already extracted in this workspace. Its 60 entries / 56 unique files and independently verified output provide a byte-for-byte reference. The source OFP, firmware images and unrelated workspace files must not be published.

## Approaches and decision

1. Native Win32 GUI and Windows CNG cryptography in C: recommended and selected by the user's requirement to use Windows internal components without additional downloads. Small portable distribution and normal Windows file dialogs; more custom UI work.
2. GTK desktop GUI in C: cross-platform, but its distribution dependencies conflict with the requested use of internal Windows components. Rejected.
3. Python GUI or a browser-based interface: quicker reuse of scripts, but conflicts with the native C, fully graphical standalone requirement. Rejected.

No command-line product is included. Test executables and development-only Python reference scripts may exist in the source repository; the shipped user application never invokes Python, a shell, or an external executable.

## Scope

The first release implements MediaTek OFP and Qualcomm OFP decoding based on the relevant bkerler/oppo_decrypt scripts, with automatic detection and explicit unsupported-format errors. Preserve and credit upstream authors and retain applicable MIT notices for adapted code.

This is an OFP extractor, not a phone flasher or a repacker. OnePlus OPS, download-tool patching/readback, automated driver installation, downloading authentication/DA binaries and split-super merging are excluded from this release. Qualcomm ZIP-wrapped OFP containers must be detected and clearly reported as unsupported in this release rather than invoking Windows Explorer, PowerShell, Python or a downloaded ZIP utility. Document this limit prominently. Ordinary firmware ZIP inputs receive a clear instruction to select the contained OFP.

## User interface

One resizable, DPI-aware desktop window, English initially, Windows 10/11 x64. Use a restrained dark navy theme, readable Segoe UI typography, teal accent, clear keyboard focus, labeled controls, and native dialogs. Keep custom painting in headers, cards and progress decoration; use native accessible controls for interactive fields, buttons and lists.

Top area: product name, DroidRooter-derived logo and selected package identity. Avoid promotional claims and filler copy; no privacy slogan or generic marketing tagline. Main flow:

1. A labeled OFP input field and always-visible **Browse OFP** button open the native file picker. Drag-and-drop is an optional shortcut, never the only way to select input. Inspection runs off the UI thread and lists detected format, project/chipset where available, input size, file count and expected output size. Browsing does not start extraction.
2. A separate labeled output field and always-visible **Choose output folder** button open the native folder picker. Allow manual path entry. Default to a new sibling folder derived from the input filename. Do not require administrator privileges.
3. Review a checkbox file list with names, logical partition labels, sizes and extraction state; **Select all**, **Clear selection**, and a search/filter field. Individual images can be selected independently. Filtering must preserve selections outside the visible results; show the total selected count and selected output size so hidden selections are apparent. Disable Extract when nothing is selected. Resolve duplicate output names deterministically without silently losing different content.
4. Options: generate a SHA-256 manifest (default on), verify written output by rereading it (default off; explain added disk reads), create a conventional eMMC SP Flash Tool scatter when an eligible OEM scatter exists (default off), and open output folder after success (default on).
5. Extract. Show current file, overall byte progress, transferred/total size, elapsed time, throughput and estimated remaining time. Cancel remains available. Show success, cancellation or failure clearly; offer Open output folder and Save log.

Prevent controls from changing active job inputs. Keep the window responsive and handle Close during an active job through an explicit cancel-and-close prompt. Logs are available in a collapsible or secondary view; routine users do not need to read them.

Before extraction, show available destination space beside the selected output size and surface actionable validation errors. An explicit **Extract selected** button starts the job; no operation starts solely from choosing a file or folder. A file-details view exposes source offset, encrypted span, expected output size and any available vendor hash for diagnosis. After completion offer **Open output folder**, **Save log**, and **Start another extraction**. Display supported formats and application version in an About/help dialog, including upstream credits and the ZIP/OPS exclusions. Partial-image extraction is fully supported; if the selected files cannot satisfy scatter references, explain why scatter generation is unavailable without selecting extra files silently.

## Extraction architecture

Pure C with Win32/common controls, native file/folder dialogs, Windows CNG AES/MD5/SHA-256, XMLLite for Qualcomm metadata, and Shell APIs only for opening the chosen output folder. CNG feedback-mode semantics must be proven against Python CFB128 vectors; use AES-ECB to implement CFB128 explicitly if necessary. No new cryptographic primitive implementation.

Keep UI, job lifecycle, archive inspection, MediaTek decoding, Qualcomm decoding, stream I/O, cryptography, scatter conversion and manifests in focused modules. Use a worker thread and throttled progress messages. Cancellation uses an event or atomic flag and is checked between bounded chunks.

Use 64-bit offsets and bounded streaming buffers (initial proposal 4 MiB per stream), avoiding whole-image allocations. Validate metadata before output: lengths, overflow, offsets, entry count, encrypted spans and path safety. Handle Unicode filesystem paths through wide-character Windows APIs. Treat metadata as untrusted. Reject absolute paths, traversal, alternate data streams, reserved device names and output paths escaping through reparse points. Reject source/destination collisions.

Do not delete or empty an existing destination. Refuse pre-existing target files and ask the user to choose a new destination; no overwrite option in v1. Preflight free space and all selected target paths. Write partial files with exclusive creation in the destination and rename only when their own extraction/verification succeeds. Remove only partial files owned by the current operation on failure/cancel; retain completed files and report partial completion. Do not publish a success manifest or converted scatter after an incomplete job.

MTK duplicate filenames: collapse byte-equivalent logical entries to one output, retaining all logical partition labels in inspection and manifest. Compare metadata spans or source content to establish equivalence. Refuse conflicting duplicates with an actionable message instead of silently overwriting.

Qualcomm: port the supported page sizes, key profiles, metadata sections and copy/decrypt distinctions from the reference, validating XML and ranges. Disable DTD/external entity resolution and bound XML size/depth. Check vendor hashes using the actual reference conventions and distinguish vendor verification from a newly generated SHA-256 manifest. Unknown keys are explicit errors.

## SP Flash Tool option

Preserve the original scatter. Generate a separately named conventional eMMC scatter only when a recognized MediaTek OEM scatter and every required referenced file are present. Preserve addresses, sizes, partition names and OEM image choices; normalize syntax and the eMMC boot-region name. Disable preloader downloading in the compatibility copy and explain that choice. Validate sparse-image structure and expanded sizes against partition bounds. Missing images or unrecognized scatter layout means no compatibility scatter, with a clear explanation; it must not turn successful extraction into a misleading flash-ready claim. Never modify image bytes. No assertion of phone compatibility, DA acceptance or flash-tool UI qualification without actual tests.

## Verification and performance

Use synthetic fixtures for supported MTK key profiles, Qualcomm page/key profiles, copies and decryptions, truncated or malicious metadata, Unicode paths, collisions, cancellation, duplicate entries, sparse images and scatter conversion. Python is allowed only in development fixture generation/reference comparisons.

Extract the full supplied F15 OFP with the native application and compare every output SHA-256 with the verified Python extraction. Exercise the actual GUI for selection, inspection, extraction, cancellation and results; capture a screenshot for the README/release. Qualcomm support is fixture-qualified unless a real Qualcomm package becomes available; state this limitation. Do not label untested formats as proven on hardware.

Measure elapsed extraction time and peak memory on the same input and disk as the Python reference with equivalent verification options; disclose cache/disk limitations and publish measured results without promising a speedup. Streaming and a responsive UI are required even if the disk limits throughput.

## Packaging and publishing

Build a release x64 GUI executable with static compiler runtime where applicable, linked only to Windows system DLLs. Inspect dependencies to confirm no Python, VC redistributable or separately distributed DLL is required. No installer required. Publish a ZIP containing the EXE, license, notices and concise usage instructions; include SHA-256 checksums.

Use an isolated new source directory, a new isolated ofp-studio source folder, after design approval. Initialize a new Git repository there. Publish to public saadnahid7/droidrooter-ofp-studio if available, with README, build instructions, license and upstream attribution, qualification table, CI build/tests and a tagged release with portable binaries. Explicitly stage reviewed source/assets only. Firmware, private paths, secrets, generated test output and large local inputs remain excluded.

## Builder discretion

The implementer may choose control layout details, internal interfaces, module boundaries, test harness tooling, icon geometry, exact C compiler flags and progress throttling. Use the already available Windows C toolchain. These choices must not add user runtime dependencies, widen format claims or weaken verification and output safety.
