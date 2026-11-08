#include "ofp.h"
#include <stdlib.h>
#include <wchar.h>
/* Finalize or discard the exact exclusively-open file object we created. Never
   close a partial file then use its pathname to reopen, rename or delete it. */
int ofp_finalize_output(HANDLE h, const wchar_t *destination, wchar_t *error) {
    size_t bytes = wcslen(destination) * sizeof(wchar_t);
    size_t n = sizeof(FILE_RENAME_INFO) + bytes;
    FILE_RENAME_INFO *info = calloc(1, n);
    if (!info)
        return ofp_error(error, L"Not enough memory to finalize output.");
    info->ReplaceIfExists = FALSE;
    info->FileNameLength = (DWORD)bytes;
    memcpy(info->FileName, destination, bytes);
    int ok = SetFileInformationByHandle(h, FileRenameInfo, info, (DWORD)n);
    DWORD code = GetLastError();
    free(info);
    return ok ? 1
              : ofp_error(error,
                          L"Cannot finalize output without replacing an existing file (error %lu).",
                          code);
}
void ofp_discard_output(HANDLE h) {
    FILE_DISPOSITION_INFO info = {TRUE};
    SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info));
}

void ofp_unlock_dirs(OfpDirLocks *d) {
    for (size_t i = 0; i < d->count; i++)
        CloseHandle(d->handles[i]);
    free(d->handles);
}
static int lock_dir(OfpDirLocks *d, const wchar_t *path, wchar_t *error) {
    HANDLE h =
        CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    if (h == INVALID_HANDLE_VALUE)
        return ofp_error(error, L"Cannot access output directory: %ls", path);
    if (!GetFileInformationByHandle(h, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        return ofp_error(error, L"Output directories cannot be links or reparse points: %ls", path);
    }
    if (d->count == d->cap) {
        size_t cap = d->cap ? d->cap * 2 : 32;
        HANDLE *p = realloc(d->handles, cap * sizeof(HANDLE));
        if (!p) {
            CloseHandle(h);
            return ofp_error(error, L"Not enough memory.");
        }
        d->handles = p;
        d->cap = cap;
    }
    d->handles[d->count++] = h;
    return 1;
}
int ofp_prepare_dirs(OfpDirLocks *locks, const wchar_t *path, int include_leaf, int create,
                     wchar_t *error) {
    /* Support local drive paths. UNC destinations are intentionally rejected to keep
       directory locking, disk-space checks and output ownership predictable in v1. */
    if (wcslen(path) < 3 || path[1] != L':' || path[2] != L'\\')
        return ofp_error(error, L"Choose a local drive output folder (for example D:\\Firmware).");
    wchar_t *copy = _wcsdup(path);
    if (!copy)
        return ofp_error(error, L"Not enough memory.");
    int ok = 1;
    for (size_t i = 3;; i++)
        if (copy[i] == L'\\' || !copy[i]) {
            int last = !copy[i];
            if (last && !include_leaf)
                break;
            wchar_t save = copy[i];
            copy[i] = 0;
            if (create && !CreateDirectoryW(copy, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                ok = ofp_error(error, L"Cannot create folder: %ls (error %lu)", copy,
                               GetLastError());
                break;
            }
            if (!lock_dir(locks, copy, error)) {
                ok = 0;
                break;
            }
            copy[i] = save;
            if (last)
                break;
        }
    free(copy);
    return ok;
}
