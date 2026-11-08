#include "ofp.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
int ofp_error(wchar_t *e, const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vswprintf(e, OFP_ERROR, fmt, ap);
    va_end(ap);
    return 0;
}
uint16_t ofp_u16(const unsigned char *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
uint32_t ofp_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint64_t ofp_u64(const unsigned char *p) {
    return ofp_u32(p) | ((uint64_t)ofp_u32(p + 4) << 32);
}
int ofp_read(HANDLE h, uint64_t at, void *p, DWORD n) {
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)at;
    DWORD got = 0;
    return SetFilePointerEx(h, pos, NULL, FILE_BEGIN) && ReadFile(h, p, n, &got, NULL) && got == n;
}
int ofp_join(wchar_t *out, const wchar_t *root, const wchar_t *name) {
    return swprintf(out, OFP_PATH, L"%ls%ls%ls", root,
                    (root[wcslen(root) - 1] == L'\\') ? L"" : L"\\", name) > 0;
}
int ofp_safe_name(const wchar_t *name) {
    if (!*name || wcslen(name) >= 510 || name[0] == L'/' || name[0] == L'\\')
        return 0;
    const wchar_t *p = name;
    while (*p) {
        wchar_t part[512];
        size_t n = 0;
        while (*p && *p != L'/' && *p != L'\\') {
            if (*p < 32 || wcschr(L"<>:\"|?*", *p))
                return 0;
            part[n++] = *p++;
        }
        part[n] = 0;
        if (!n || part[n - 1] == L'.' || part[n - 1] == L' ')
            return 0;
        wchar_t *dot = wcschr(part, L'.');
        if (dot)
            *dot = 0;
        size_t stem = wcslen(part);
        while (stem && part[stem - 1] == L' ')
            part[--stem] = 0;
        if (!_wcsicmp(part, L"CON") || !_wcsicmp(part, L"PRN") || !_wcsicmp(part, L"AUX") ||
            !_wcsicmp(part, L"NUL") || !_wcsicmp(part, L"CONIN$") || !_wcsicmp(part, L"CONOUT$"))
            return 0;
        if (wcslen(part) == 4 && (!_wcsnicmp(part, L"COM", 3) || !_wcsnicmp(part, L"LPT", 3)) &&
            ((part[3] >= L'0' && part[3] <= L'9') || part[3] == 0xB9 || part[3] == 0xB2 ||
             part[3] == 0xB3))
            return 0;
        if (*p) {
            p++;
            if (!*p)
                return 0;
        }
    }
    return 1;
}
int ofp_add_entry(OfpArchive *a, OfpEntry *e, HANDLE h, wchar_t *error) {
    if (a->cancel && InterlockedCompareExchange(a->cancel, 0, 0))
        return ofp_error(error, L"Inspection cancelled.");
    if (!ofp_safe_name(e->name))
        return ofp_error(error, L"Unsafe archive path: %ls", e->name);
    for (wchar_t *p = e->name; *p; p++)
        if (*p == L'/')
            *p = L'\\';
    if (e->offset > a->source_size || e->size > a->source_size - e->offset ||
        e->encrypted > e->size)
        return ofp_error(error, L"Invalid source bounds: %ls", e->name);
    for (int i = 0; i < a->count; i++) {
        const wchar_t *other = a->entries[i].name;
        size_t oldlen = wcslen(other), newlen = wcslen(e->name);
        if ((oldlen < newlen && !_wcsnicmp(other, e->name, oldlen) && e->name[oldlen] == L'\\') ||
            (newlen < oldlen && !_wcsnicmp(other, e->name, newlen) && other[newlen] == L'\\'))
            return ofp_error(error, L"Archive path is both a file and a directory: %ls", e->name);
    }
    for (int i = 0; i < a->count; i++)
        if (!_wcsicmp(a->entries[i].name, e->name)) {
            OfpEntry *old = &a->entries[i];
            if (old->size != e->size || old->encrypted != e->encrypted ||
                _stricmp(old->vendor_sha, e->vendor_sha) ||
                _stricmp(old->vendor_md5, e->vendor_md5))
                return ofp_error(error, L"Conflicting duplicate filename: %ls", e->name);
            unsigned char left[32768], right[32768];
            for (uint64_t p = 0; p < e->size;) {
                DWORD n = (DWORD)((e->size - p) > sizeof(left) ? sizeof(left) : e->size - p);
                if (a->cancel && InterlockedCompareExchange(a->cancel, 0, 0))
                    return ofp_error(error, L"Inspection cancelled.");
                if (!ofp_read(h, old->offset + p, left, n) ||
                    !ofp_read(h, e->offset + p, right, n) || memcmp(left, right, n))
                    return ofp_error(error, L"Conflicting duplicate contents: %ls", e->name);
                p += n;
            }
            size_t chars = wcslen(old->aliases) + wcslen(e->label) + 4;
            wchar_t *aliases = realloc(old->aliases, chars * sizeof(wchar_t));
            if (!aliases)
                return ofp_error(error, L"Not enough memory for logical labels.");
            old->aliases = aliases;
            wcscat(aliases, L" | ");
            wcscat(aliases, e->label);
            a->logical_count++;
            return 1;
        }
    if (a->count >= OFP_MAX_ENTRIES || UINT64_MAX - a->total < e->size)
        return ofp_error(error, L"Archive contains too many files or oversized totals.");
    e->selected = 1;
    e->aliases = _wcsdup(e->label);
    if (!e->aliases)
        return ofp_error(error, L"Not enough memory for logical labels.");
    a->entries[a->count++] = *e;
    a->logical_count++;
    a->total += e->size;
    return 1;
}
int ofp_inspect(const wchar_t *p, OfpArchive *a, wchar_t *error) {
    return ofp_inspect_cancel(p, a, NULL, error);
}
int ofp_inspect_cancel(const wchar_t *p, OfpArchive *a, volatile LONG *cancel, wchar_t *error) {
    memset(a, 0, sizeof(*a));
    DWORD len = GetFullPathNameW(p, OFP_PATH, a->path, NULL);
    a->cancel = cancel;
    if (!len || len >= OFP_PATH)
        return ofp_error(error, L"Input path is too long or invalid.");
    HANDLE h = CreateFileW(a->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return ofp_error(error, L"Cannot open OFP (Windows error %lu).", GetLastError());
    LARGE_INTEGER size;
    unsigned char magic[16];
    int ok = 0;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 108 || !ofp_read(h, 0, magic, 16)) {
        ofp_error(error, L"File is too small or unreadable.");
        goto done;
    }
    a->source_size = (uint64_t)size.QuadPart;
    if (!GetFileInformationByHandle(h, &a->source_info)) {
        ofp_error(error, L"Cannot identify source file.");
        goto done;
    }
    if (magic[0] == 'P' && magic[1] == 'K') {
        ofp_error(error, L"ZIP-wrapped packages are not supported in this version. Select a raw "
                         L"MTK or Qualcomm OFP.");
        goto done;
    }
    a->entries = calloc(OFP_MAX_ENTRIES, sizeof(OfpEntry));
    if (!a->entries) {
        ofp_error(error, L"Not enough memory.");
        goto done;
    }
    /* Qualcomm footer magic is independent of encryption keys. */
    unsigned char footer[4];
    int qc = 0;
    const unsigned pages[] = {512, 4096};
    for (int i = 0; i < 2; i++)
        if (a->source_size >= pages[i] && ofp_read(h, a->source_size - pages[i] + 16, footer, 4) &&
            ofp_u32(footer) == 0x7cef)
            qc = 1;
    ok = qc ? ofp_qc(h, a, error) : ofp_mtk(h, a, error);
done:
    CloseHandle(h);
    if (!ok)
        ofp_free(a);
    return ok;
}
void ofp_free(OfpArchive *a) {
    for (int i = 0; i < a->count; i++)
        free(a->entries[i].aliases);
    free(a->entries);
    free(a->xml);
    memset(a, 0, sizeof(*a));
}
