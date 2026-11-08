#include "magisk.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static int exists(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    if (a != INVALID_FILE_ATTRIBUTES)
        return 1;
    DWORD e = GetLastError();
    return e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND;
}
static int cancelled(void *c, wchar_t *e) {
    return dr_cancelled(c) ? (ofp_error(e, L"Cancelled. No patched image was published."), 1) : 0;
}
int ofp_patch_image(const wchar_t *source, const wchar_t *output, const wchar_t *partition,
                    void *cancel, OfpProgress cb, void *ctx, wchar_t *error) {
    HANDLE input = INVALID_HANDLE_VALUE, apk = INVALID_HANDLE_VALUE, image = INVALID_HANDLE_VALUE,
           report = INVALID_HANDLE_VALUE;
    OfpDirLocks locks = {0}, temp_locks = {0};
    wchar_t *dest = calloc(OFP_PATH, sizeof(wchar_t)),
            *metadata = calloc(OFP_PATH, sizeof(wchar_t)),
            *partial = calloc(OFP_PATH, sizeof(wchar_t)),
            *meta_partial = calloc(OFP_PATH, sizeof(wchar_t)),
            *apk_path = calloc(OFP_PATH, sizeof(wchar_t));
    unsigned char *stock = NULL;
    void *result = NULL;
    int ok = 0;
    error[0] = 0;
    if (!dest || !metadata || !partial || !meta_partial || !apk_path) {
        ofp_error(error, L"Not enough memory.");
        goto done;
    }
    if (!cancel) {
        ofp_error(error, L"Missing cancellation token.");
        goto done;
    }
    if (cancelled(cancel, error))
        goto done;
    size_t plen = wcslen(partition);
    if (plen > 128) {
        ofp_error(error, L"Pre-init override is too long.");
        goto done;
    }
    char preinit[129];
    for (size_t i = 0; i < plen; i++) {
        wchar_t ch = partition[i];
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || wcschr(L"._-", ch))) {
            ofp_error(error, L"Pre-init must be a partition name, not a path or command.");
            goto done;
        }
        preinit[i] = (char)ch;
    }
    preinit[plen] = 0;
    DWORD len = GetFullPathNameW(output, OFP_PATH, dest, NULL);
    if (!len || len >= OFP_PATH - 100) {
        ofp_error(error, L"Invalid output path.");
        goto done;
    }
    const wchar_t *leaf = wcsrchr(dest, L'\\');
    if (!leaf || !ofp_safe_name(leaf + 1)) {
        ofp_error(error, L"Choose a valid new image filename.");
        goto done;
    }
    swprintf(metadata, OFP_PATH, L"%ls.provenance.json", dest);
    swprintf(partial, OFP_PATH, L"%ls.patch.partial", dest);
    swprintf(meta_partial, OFP_PATH, L"%ls.patch.partial", metadata);
    if (exists(dest) || exists(metadata) || exists(partial) || exists(meta_partial)) {
        ofp_error(error, L"Output, report or partial file already exists. Choose a new output "
                         L"filename; nothing is overwritten.");
        goto done;
    }
    if (!ofp_prepare_dirs(&locks, dest, 0, 1, error))
        goto done;
    input = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    if (input == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !GetFileSizeEx(input, &size) || size.QuadPart < 4096 || size.QuadPart > 134217728) {
        ofp_error(error, L"Cannot read a standard stock boot/init_boot image (maximum 128 MiB; "
                         L"file links are not accepted).");
        goto done;
    }
    size_t stock_size = (size_t)size.QuadPart;
    stock = malloc(stock_size);
    if (!stock) {
        ofp_error(error, L"Not enough memory for boot image.");
        goto done;
    }
    if (cb)
        cb(ctx, 0, 0, 0, L"Reading a protected snapshot of the stock image...");
    for (size_t pos = 0; pos < stock_size;) {
        if (cancelled(cancel, error))
            goto done;
        DWORD n = (DWORD)((stock_size - pos) > 4194304 ? 4194304 : stock_size - pos);
        if (!ofp_read(input, pos, stock + pos, n)) {
            ofp_error(error, L"Cannot read stock image.");
            goto done;
        }
        pos += n;
    }
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(201), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
    DWORD apk_size = resource ? SizeofResource(NULL, resource) : 0;
    const void *apk_bytes = loaded ? LockResource(loaded) : NULL;
    char hash[65];
    if (!apk_bytes || apk_size != 10679283 ||
        !ofp_digest(BCRYPT_SHA256_ALGORITHM, apk_bytes, apk_size, hash, 32) ||
        strcmp(hash, "89cefac0a318afa39fb3880c45c87b036c05bc1ebf02a7a715b7f8d7c26ac6e7")) {
        ofp_error(error, L"Bundled official Magisk 31.1 resource is missing or damaged.");
        goto done;
    }
    wchar_t temp[OFP_PATH];
    DWORD tl = GetTempPathW(OFP_PATH, temp);
    unsigned char random[16];
    if (!tl || tl >= OFP_PATH - 80 ||
        BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        ofp_error(error, L"Cannot prepare a private APK snapshot.");
        goto done;
    }
    swprintf(apk_path, OFP_PATH,
             L"%lsdr-magisk-%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.apk",
             temp, random[0], random[1], random[2], random[3], random[4], random[5], random[6],
             random[7], random[8], random[9], random[10], random[11], random[12], random[13],
             random[14], random[15]);
    if (!ofp_prepare_dirs(&temp_locks, apk_path, 0, 0, error))
        goto done;
    apk = CreateFileW(apk_path, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, NULL,
                      CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    DWORD wrote;
    if (apk == INVALID_HANDLE_VALUE || !WriteFile(apk, apk_bytes, apk_size, &wrote, NULL) ||
        wrote != apk_size || !FlushFileBuffers(apk)) {
        ofp_error(error, L"Cannot stage bundled Magisk data in the temporary folder.");
        goto done;
    }
    if (cancelled(cancel, error))
        goto done;
    if (cb)
        cb(ctx, 1, 0, 0,
           L"Validating AVB, patching Magisk ramdisk and checking the rebuilt image...");
    char native_error[3072] = {0};
    result = dr_patch(stock, stock_size, apk_path, wcslen(apk_path), preinit, cancel, native_error,
                      sizeof(native_error));
    if (!result) {
        if (!MultiByteToWideChar(CP_UTF8, 0, native_error, -1, error, OFP_ERROR))
            ofp_error(error, L"Native patch engine rejected this image.");
        goto done;
    }
    if (cancelled(cancel, error))
        goto done;
    size_t image_size = 0, report_size = 0;
    const unsigned char *bytes = dr_result_bytes(result, &image_size),
                        *json = dr_result_report(result, &report_size);
    if (!bytes || image_size != stock_size || !json || !report_size || report_size > 1048576) {
        ofp_error(error, L"Native patch result failed bounds validation.");
        goto done;
    }
    image = CreateFileW(partial, GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (image == INVALID_HANDLE_VALUE) {
        ofp_error(error, L"Cannot create owned patched-image output.");
        goto done;
    }
    if (cb)
        cb(ctx, 2, 0, image_size, L"Writing patched image...");
    for (size_t pos = 0; pos < image_size;) {
        if (cancelled(cancel, error))
            goto done;
        DWORD n = (DWORD)((image_size - pos) > 4194304 ? 4194304 : image_size - pos);
        if (!WriteFile(image, bytes + pos, n, &wrote, NULL) || wrote != n) {
            ofp_error(error, L"Patched image write failed.");
            goto done;
        }
        pos += n;
        if (cb)
            cb(ctx, 2, pos, image_size, L"Writing patched image...");
    }
    if (!FlushFileBuffers(image)) {
        ofp_error(error, L"Cannot flush patched image.");
        goto done;
    }
    /* Verify bytes through the same exclusive handle before publication. */
    unsigned char *verify = malloc(4194304);
    if (!verify) {
        ofp_error(error, L"Not enough memory to verify output.");
        goto done;
    }
    int verified = 1;
    for (size_t pos = 0; pos < image_size;) {
        if (cancelled(cancel, error)) {
            verified = 0;
            break;
        }
        DWORD n = (DWORD)((image_size - pos) > 4194304 ? 4194304 : image_size - pos);
        if (!ofp_read(image, pos, verify, n) || memcmp(verify, bytes + pos, n)) {
            ofp_error(error, L"Written patched image differs from the validated result.");
            verified = 0;
            break;
        }
        pos += n;
    }
    free(verify);
    if (!verified)
        goto done;
    report = CreateFileW(meta_partial, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (report == INVALID_HANDLE_VALUE ||
        !WriteFile(report, json, (DWORD)report_size, &wrote, NULL) || wrote != report_size ||
        !FlushFileBuffers(report)) {
        ofp_error(error, L"Cannot write patch provenance report.");
        goto done;
    }
    if (cancelled(cancel, error))
        goto done;
    if (!ofp_finalize_output(report, metadata, error) || !ofp_finalize_output(image, dest, error))
        goto done;
    ok = 1;
    if (cb)
        cb(ctx, 3, image_size, image_size, L"Patched image and provenance report saved.");
done:
    if (!ok) {
        if (image != INVALID_HANDLE_VALUE)
            ofp_discard_output(image);
        if (report != INVALID_HANDLE_VALUE)
            ofp_discard_output(report);
    }
    if (image != INVALID_HANDLE_VALUE)
        CloseHandle(image);
    if (report != INVALID_HANDLE_VALUE)
        CloseHandle(report);
    if (apk != INVALID_HANDLE_VALUE) {
        ofp_discard_output(apk);
        CloseHandle(apk);
    }
    if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
    if (result)
        dr_result_free(result);
    free(stock);
    free(dest);
    free(metadata);
    free(partial);
    free(meta_partial);
    free(apk_path);
    ofp_unlock_dirs(&locks);
    ofp_unlock_dirs(&temp_locks);
    if (!ok && !error[0])
        ofp_error(error, L"Patch operation failed.");
    return ok;
}
