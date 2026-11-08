# Qualification

## Correctness

- Full OPPO F15 / CPH2001 C.53 MediaTek OFP inspected: 60 logical entries, 56 unique output files.
- All 56 native output SHA-256 values match the previously independently verified Python extraction.
- Native qualification extraction also reread written files and verified their SHA-256 values.
- Real F15 OEM scatter converted to a separate eMMC compatibility scatter after checking references and expanded sparse-image sizes.
- No phone flashing or SP Flash Tool UI loading was performed.
- Qualcomm formats are tested with synthetic independently encrypted fixtures, not a real Qualcomm package.

## Automated and GUI checks

The behavioral suite covers encrypted/plain MTK spans, known MTK key profiles,
Qualcomm copy/decrypt sections and both page sizes, checksums, selective extraction,
Unicode/nested paths, output collisions, malicious path aliases, directory junctions,
duplicate files, truncated metadata, unknown keys, cancellation, valid scatter
conversion, missing references and malformed sparse images.

Actual GUI checks cover native file/folder dialogs, inspection, selection clearing,
individual selection, extraction of only the selected file, manifest creation and
readback verification. Additional release checks and measurements are recorded below.

## Performance

The initial full native extraction with SHA-256 generation, readback verification
and scatter conversion completed in 334.0 seconds on the local test disk. This
includes additional reads and is not an extraction-only comparison with Python.

Extraction-only benchmark, same full F15 source on the same local disk, native first and Python second; hashes/readback/scatter disabled for both:

| Implementation | Wall time | Peak working set |
| --- | ---: | ---: |
| Native C engine | 231.4 s | 8.93 MiB |
| Upstream Python MediaTek decryptor | 224.7 s | 10.80 MiB |

The native run used approximately 17.4% less peak working memory. Python was about 6.7 seconds faster in this disk-bound run, so this release makes no extraction-speedup claim. The native engine also flushes each completed file and collapses identical duplicate entries; the Python reference rewrites those entries and relies on normal buffered close behavior.
Single-machine, sequential disk runs are sensitive to filesystem cache, file
placement, antivirus and disk contention. They do not prove a universal speedup.

## Distribution

The Windows x64 EXE is linked with the compiler runtime statically. Its imported
DLLs are Windows system components: CNG, common controls/dialogs, DWM/GDI, kernel,
Windows UCRT API sets, COM, shell, User32, UXTheme and XMLLite. No Python, OpenSSL,
GTK, MinGW runtime DLL or separately installed VC redistributable is required.

## Final release recheck

After the independent code review and finalization/cancellation fixes, a fresh full F15 extraction completed successfully in 313.7 seconds with SHA-256 generation, same-handle readback verification and scatter conversion enabled. All 56 output hashes again match the verified Python reference. Logical aliases are retained in OFP-Studio-report.txt, and no partial files remain. All 29 behavioral tests pass. GUI cancellation retained completed images, removed the unfinished image, published no success manifest, and displayed a truthful incomplete-verification status.
