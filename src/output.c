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
