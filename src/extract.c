#include "ofp.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define CHUNK (4 * 1024 * 1024)
static int absent(const wchar_t *p, wchar_t *error) {
    DWORD attr = GetFileAttributesW(p);
    if (attr != INVALID_FILE_ATTRIBUTES)
        return ofp_error(error, L"An output already exists. Choose a new folder: %ls", p);
    DWORD e = GetLastError();
    if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
        return ofp_error(error, L"Cannot check output path (error %lu): %ls", e, p);
    return 1;
}
static int write_all(HANDLE h, const void *p, DWORD n) {
    DWORD wrote;
    return WriteFile(h, p, n, &wrote, NULL) && wrote == n;
}
static int compare_names(const void *a, const void *b) {
    return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b);
}
static int namespace_valid(OfpArchive *a, int manifest, volatile LONG *cancel, wchar_t *error) {
    size_t count = 0, capacity = (size_t)a->count * 2 + 4;
    wchar_t **names = calloc(capacity, sizeof(*names));
    int ok = 0;
    if (!names)
        return ofp_error(error, L"Not enough memory.");
    for (int i = 0; i < a->count + 2; i++) {
        const wchar_t *name =
            i < a->count
                ? (a->entries[i].selected ? a->entries[i].name : NULL)
                : (manifest ? (i == a->count ? L"SHA256SUMS.txt" : L"OFP-Studio-report.txt")
                            : NULL);
        if (!name)
            continue;
        wchar_t temp[600];
        swprintf(temp, 600, L"%ls.ofp-studio.partial", name);
        names[count++] = _wcsdup(name);
        names[count++] = _wcsdup(temp);
        if (!names[count - 1] || !names[count - 2]) {
            ofp_error(error, L"Not enough memory.");
            goto done;
        }
    }
    qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0; i < count; i++) {
        if (InterlockedCompareExchange(cancel, 0, 0)) {
            ofp_error(error, L"Cancelled during output preflight.");
            goto done;
        }
        if (i && !_wcsicmp(names[i - 1], names[i])) {
            ofp_error(error, L"Selected images conflict with an output or temporary filename: %ls",
                      names[i]);
            goto done;
        }
        wchar_t prefix[600];
        wcscpy(prefix, names[i]);
        for (wchar_t *p = prefix; *p; p++)
            if (*p == L'\\') {
                *p = 0;
                wchar_t *key = prefix;
                void *found = bsearch(&key, names, count, sizeof(*names), compare_names);
                *p = L'\\';
                if (found) {
                    ofp_error(error, L"An output path is both a file and a directory: %ls",
                              names[i]);
                    goto done;
                }
            }
    }
    ok = 1;
done:
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
    return ok;
}
static int reread_hash(HANDLE h, char *digest, volatile LONG *cancel, OfpProgress cb, void *ctx,
                       int index, uint64_t done, uint64_t total, wchar_t *error) {
    LARGE_INTEGER zero = {0};
    if (!SetFilePointerEx(h, zero, NULL, FILE_BEGIN))
        return ofp_error(error, L"Cannot reread extracted file.");
    OfpHash hash = {0};
    unsigned char *buf = malloc(CHUNK);
    int ok = buf && ofp_hash_begin(&hash, BCRYPT_SHA256_ALGORITHM);
    DWORD n = 0;
    while (ok) {
        if (InterlockedCompareExchange(cancel, 0, 0)) {
            ok = ofp_error(error, L"Cancelled. Completed files were retained.");
            break;
        }
        if (!ReadFile(h, buf, CHUNK, &n, NULL)) {
            ok = ofp_error(error, L"Readback verification failed.");
            break;
        }
        if (!n)
            break;
        ok = ofp_hash_add(&hash, buf, n);
        if (cb)
            cb(ctx, index, done, total, L"Verifying written image");
    }
    if (ok)
        ok = ofp_hash_end(&hash, digest, 32);
    ofp_hash_close(&hash);
    free(buf);
    return ok;
}
int ofp_extract(OfpArchive *a, const wchar_t *destination, const OfpOptions *o, OfpProgress cb,
                void *ctx, volatile LONG *cancel, wchar_t *error) {
    wchar_t *dest = calloc(OFP_PATH, sizeof(wchar_t)), *final = calloc(OFP_PATH, sizeof(wchar_t)),
            *partial = calloc(OFP_PATH, sizeof(wchar_t));
    unsigned char *buf = NULL;
    HANDLE src = INVALID_HANDLE_VALUE, out = INVALID_HANDLE_VALUE;
    HANDLE artifacts[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    OfpDirLocks locks = {0};
    int ok = 0, partial_owned = 0;
    OfpHash sha = {0}, prefix_sha = {0}, md5 = {0};
    OfpCipher cipher = {0};
    uint64_t total = 0, done = 0;
    int selected = 0;
    error[0] = 0;
    if (!dest || !final || !partial) {
        ofp_error(error, L"Not enough memory.");
        goto cleanup;
    }
    DWORD len = GetFullPathNameW(destination, OFP_PATH, dest, NULL);
    if (!len || len >= OFP_PATH - 1024) {
        ofp_error(error, L"Invalid or overly long destination.");
        goto cleanup;
    }
    while (wcslen(dest) > 3 && dest[wcslen(dest) - 1] == L'\\')
        dest[wcslen(dest) - 1] = 0;
    for (int i = 0; i < a->count; i++) {
        a->entries[i].completed = 0;
        a->entries[i].sha256[0] = 0;
        if (a->entries[i].selected) {
            selected++;
            total += a->entries[i].size;
        }
    }
    if (!selected) {
        ofp_error(error, L"Select at least one image to extract.");
        goto cleanup;
    }
    if (!namespace_valid(a, o->manifest, cancel, error))
        goto cleanup;
    src = CreateFileW(a->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                      FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    if (src == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(src, &info) ||
        info.dwVolumeSerialNumber != a->source_info.dwVolumeSerialNumber ||
        info.nFileIndexHigh != a->source_info.nFileIndexHigh ||
        info.nFileIndexLow != a->source_info.nFileIndexLow ||
        info.nFileSizeHigh != a->source_info.nFileSizeHigh ||
        info.nFileSizeLow != a->source_info.nFileSizeLow ||
        CompareFileTime(&info.ftLastWriteTime, &a->source_info.ftLastWriteTime)) {
        ofp_error(error, L"Source changed since inspection. Browse the OFP again.");
        goto cleanup;
    }
    if (!ofp_prepare_dirs(&locks, dest, 1, 1, error))
        goto cleanup;
    ULARGE_INTEGER freebytes;
    if (!GetDiskFreeSpaceExW(dest, &freebytes, NULL, NULL) ||
        freebytes.QuadPart < total + 1024 * 1024) {
        ofp_error(error, L"Not enough destination disk space.");
        goto cleanup;
    }
    /* Preflight the complete selected set before writing any images. */
    for (int i = 0; i < a->count; i++)
        if (a->entries[i].selected) {
            OfpEntry *e = &a->entries[i];
            if (!ofp_join(final, dest, e->name) ||
                swprintf(partial, OFP_PATH, L"%ls.ofp-studio.partial", final) < 0) {
                ofp_error(error, L"Output path too long.");
                goto cleanup;
            }
            if (!absent(final, error) || !absent(partial, error))
                goto cleanup;
            if (!ofp_prepare_dirs(&locks, final, 0, 1, error))
                goto cleanup;
        }
    if (o->manifest) {
        ofp_join(final, dest, L"SHA256SUMS.txt");
        swprintf(partial, OFP_PATH, L"%ls.ofp-studio.partial", final);
        if (!absent(final, error) || !absent(partial, error))
            goto cleanup;
        for (int i = 0; i < a->count; i++)
            if (a->entries[i].selected && !_wcsicmp(a->entries[i].name, L"SHA256SUMS.txt")) {
                ofp_error(error, L"Archive conflicts with manifest filename. Disable manifest or "
                                 L"deselect that file.");
                goto cleanup;
            }
    }
    if (o->manifest) {
        ofp_join(final, dest, L"OFP-Studio-report.txt");
        swprintf(partial, OFP_PATH, L"%ls.ofp-studio.partial", final);
        if (!absent(final, error) || !absent(partial, error))
            goto cleanup;
    }
    buf = malloc(CHUNK);
    if (!buf) {
        ofp_error(error, L"Not enough memory.");
        goto cleanup;
    }
    for (int i = 0; i < a->count; i++)
        if (a->entries[i].selected) {
            OfpEntry *e = &a->entries[i];
            if (InterlockedCompareExchange(cancel, 0, 0)) {
                ofp_error(error, L"Cancelled. Completed files were retained.");
                goto cleanup;
            }
            ofp_join(final, dest, e->name);
            swprintf(partial, OFP_PATH, L"%ls.ofp-studio.partial", final);
            out = CreateFileW(partial, GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (out == INVALID_HANDLE_VALUE) {
                ofp_error(error, L"Cannot create output (error %lu): %ls", GetLastError(), e->name);
                goto cleanup;
            }
            partial_owned = 1;
            int need_hash = o->manifest || o->verify || *e->vendor_sha || *e->vendor_md5;
            if ((need_hash && (!ofp_hash_begin(&sha, BCRYPT_SHA256_ALGORITHM) ||
                               !ofp_hash_begin(&prefix_sha, BCRYPT_SHA256_ALGORITHM) ||
                               !ofp_hash_begin(&md5, BCRYPT_MD5_ALGORITHM))) ||
                (e->encrypted && !ofp_cipher_begin(&cipher, a->key, a->iv))) {
                ofp_error(error, L"Windows cryptography initialization failed.");
                goto cleanup;
            }
            uint64_t pos = 0;
            while (pos < e->size) {
                if (InterlockedCompareExchange(cancel, 0, 0)) {
                    ofp_error(error, L"Cancelled. Completed files were retained.");
                    goto cleanup;
                }
                DWORD n = (DWORD)((e->size - pos) > CHUNK ? CHUNK : e->size - pos);
                if (!ofp_read(src, e->offset + pos, buf, n)) {
                    ofp_error(error, L"Source read failed: %ls", e->name);
                    goto cleanup;
                }
                size_t encrypted =
                    (size_t)(pos < e->encrypted
                                 ? ((e->encrypted - pos) < n ? e->encrypted - pos : n)
                                 : 0);
                if (encrypted && !ofp_cipher_decrypt(&cipher, buf, encrypted)) {
                    ofp_error(error, L"AES decryption failed.");
                    goto cleanup;
                }
                if (need_hash) {
                    size_t prefix =
                        (size_t)(pos < 0x40000 ? ((0x40000 - pos) < n ? 0x40000 - pos : n) : 0);
                    if (!ofp_hash_add(&sha, buf, n) ||
                        (prefix && (!ofp_hash_add(&prefix_sha, buf, prefix) ||
                                    !ofp_hash_add(&md5, buf, prefix)))) {
                        ofp_error(error, L"Hash calculation failed.");
                        goto cleanup;
                    }
                }
                if (!write_all(out, buf, n)) {
                    ofp_error(error, L"Output write failed (error %lu): %ls", GetLastError(),
                              e->name);
                    goto cleanup;
                }
                pos += n;
                done += n;
                if (cb)
                    cb(ctx, i, done, total, L"Extracting image");
            }
            ofp_cipher_close(&cipher);
            if (InterlockedCompareExchange(cancel, 0, 0)) {
                ofp_error(error, L"Cancelled. Completed files were retained.");
                goto cleanup;
            }
            if (!FlushFileBuffers(out)) {
                ofp_error(error, L"Cannot flush output: %ls", e->name);
                goto cleanup;
            }
            if (need_hash) {
                char prefix[65], md5hex[33];
                if (!ofp_hash_end(&sha, e->sha256, 32) || !ofp_hash_end(&prefix_sha, prefix, 32) ||
                    !ofp_hash_end(&md5, md5hex, 16)) {
                    ofp_error(error, L"Cannot finalize hashes.");
                    goto cleanup;
                }
                int has_vendor = *e->vendor_sha || *e->vendor_md5;
                int matches = (*e->vendor_sha && (!_stricmp(e->vendor_sha, e->sha256) ||
                                                  !_stricmp(e->vendor_sha, prefix))) ||
                              (*e->vendor_md5 && !_stricmp(e->vendor_md5, md5hex));
                if (has_vendor && !matches) {
                    ofp_error(error, L"Vendor checksum mismatch: %ls", e->name);
                    goto cleanup;
                }
            }
            if (o->verify) {
                char reread[65];
                if (!reread_hash(out, reread, cancel, cb, ctx, i, done, total, error))
                    goto cleanup;
                if (strcmp(reread, e->sha256)) {
                    ofp_error(error, L"Readback SHA-256 mismatch: %ls", e->name);
                    goto cleanup;
                }
            }
            if (!ofp_finalize_output(out, final, error)) {
                ofp_error(error, L"Cannot finalize output (target may already exist): %ls",
                          e->name);
                goto cleanup;
            }
            partial_owned = 0;
            CloseHandle(out);
            out = INVALID_HANDLE_VALUE;
            e->completed = 1;
            if (cb)
                cb(ctx, i, done, total, L"Image complete");
        }
    if (InterlockedCompareExchange(cancel, 0, 0)) {
        ofp_error(error, L"Cancelled. Completed files were retained.");
        goto cleanup;
    }
    if (o->manifest) {
        if (cb)
            cb(ctx, -2, done, total, L"Writing extraction metadata");
        if (InterlockedCompareExchange(cancel, 0, 0)) {
            ofp_error(error, L"Cancelled before metadata publication.");
            goto cleanup;
        }
        ofp_join(final, dest, L"SHA256SUMS.txt");
        swprintf(partial, OFP_PATH, L"%ls.ofp-studio.partial", final);
        out = CreateFileW(partial, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, NULL);
        if (out == INVALID_HANDLE_VALUE) {
            ofp_error(error, L"Cannot create manifest.");
            goto cleanup;
        }
        partial_owned = 1;
        for (int i = 0; i < a->count; i++)
            if (a->entries[i].completed) {
                if (InterlockedCompareExchange(cancel, 0, 0)) {
                    ofp_error(error, L"Cancelled before manifest publication.");
                    goto cleanup;
                }
                char name[2048], line[2200];
                WideCharToMultiByte(CP_UTF8, 0, a->entries[i].name, -1, name, sizeof(name), NULL,
                                    NULL);
                int n = snprintf(line, sizeof(line), "%s  %s\r\n", a->entries[i].sha256, name);
                if (n < 0 || !write_all(out, line, (DWORD)n)) {
                    ofp_error(error, L"Cannot write manifest.");
                    goto cleanup;
                }
            }
        if (!FlushFileBuffers(out)) {
            ofp_error(error, L"Cannot flush manifest.");
            goto cleanup;
        }
        if (!ofp_finalize_output(out, final, error)) {
            ofp_error(error, L"Cannot finalize manifest.");
            goto cleanup;
        }
        partial_owned = 0;
        artifacts[0] = out;
        out = INVALID_HANDLE_VALUE;
        ofp_join(final, dest, L"OFP-Studio-report.txt");
        swprintf(partial, OFP_PATH, L"%ls.ofp-studio.partial", final);
        out = CreateFileW(partial, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, NULL);
        if (out == INVALID_HANDLE_VALUE) {
            ofp_error(error, L"Cannot create extraction report.");
            goto cleanup;
        }
        partial_owned = 1;
        for (int i = 0; i < a->count; i++)
            if (a->entries[i].completed) {
                if (InterlockedCompareExchange(cancel, 0, 0)) {
                    ofp_error(error, L"Cancelled before report publication.");
                    goto cleanup;
                }
                OfpEntry *e = &a->entries[i];
                const wchar_t *labels = e->aliases ? e->aliases : e->label;
                int n = WideCharToMultiByte(CP_UTF8, 0, labels, -1, NULL, 0, NULL, NULL);
                char *utf = malloc(n);
                char name[2048], head[2300];
                if (!utf) {
                    ofp_error(error, L"Not enough memory.");
                    goto cleanup;
                }
                WideCharToMultiByte(CP_UTF8, 0, labels, -1, utf, n, NULL, NULL);
                WideCharToMultiByte(CP_UTF8, 0, e->name, -1, name, sizeof(name), NULL, NULL);
                int len = snprintf(head, sizeof(head),
                                   "%s\r\nSHA-256: %s\r\nLogical partitions: ", name, e->sha256);
                int good = write_all(out, head, (DWORD)len) && write_all(out, utf, (DWORD)n - 1) &&
                           write_all(out, "\r\n\r\n", 4);
                free(utf);
                if (!good) {
                    ofp_error(error, L"Cannot write extraction report.");
                    goto cleanup;
                }
            }
        if (!FlushFileBuffers(out) || !ofp_finalize_output(out, final, error))
            goto cleanup;
        partial_owned = 0;
        artifacts[1] = out;
        out = INVALID_HANDLE_VALUE;
    }
    if (o->scatter) {
        wchar_t note[OFP_ERROR] = {0};
        if (!ofp_scatter(dest, a, cancel, &artifacts[2], note)) {
            if (cb)
                cb(ctx, -1, done, total,
                   note); /* Extraction succeeded; scatter eligibility is separate. */
        }
    }
    if (InterlockedCompareExchange(cancel, 0, 0)) {
        ofp_error(error, L"Cancelled. Completed files were retained.");
        goto cleanup;
    }
    ok = 1;
cleanup:
    for (int k = 0; k < 3; k++)
        if (artifacts[k] != INVALID_HANDLE_VALUE) {
            if (!ok)
                ofp_discard_output(artifacts[k]);
            CloseHandle(artifacts[k]);
        }
    if (out != INVALID_HANDLE_VALUE) {
        if (partial_owned)
            ofp_discard_output(out);
        CloseHandle(out);
    }
    ofp_hash_close(&sha);
    ofp_hash_close(&prefix_sha);
    ofp_hash_close(&md5);
    ofp_cipher_close(&cipher);
    if (src != INVALID_HANDLE_VALUE)
        CloseHandle(src);
    ofp_unlock_dirs(&locks);
    free(dest);
    free(final);
    free(partial);
    free(buf);
    if (!ok && !error[0])
        ofp_error(error, L"Extraction failed.");
    return ok;
}
