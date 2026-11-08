#include "ofp.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
int ofp_sparse_size(const wchar_t *path, uint64_t *expanded, volatile LONG *cancel,
                    wchar_t *error) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return ofp_error(error, L"Scatter skipped: cannot read referenced image.");
    LARGE_INTEGER length;
    unsigned char hdr[28];
    int ok = 0;
    if (!GetFileSizeEx(h, &length) || length.QuadPart < 0) {
        ofp_error(error, L"Cannot read image size.");
        goto done;
    }
    *expanded = (uint64_t)length.QuadPart;
    if (*expanded >= 4 && !ofp_read(h, 0, hdr, 4)) {
        ofp_error(error, L"Cannot read referenced image header.");
        goto done;
    }
    if (*expanded < 4 || ofp_u32(hdr) != 0xed26ff3a) {
        ok = 1;
        goto done;
    }
    if (*expanded < 28 || !ofp_read(h, 0, hdr, 28)) {
        ofp_error(error, L"Truncated sparse header.");
        goto done;
    }
    unsigned fh = ofp_u16(hdr + 8), ch = ofp_u16(hdr + 10), bs = ofp_u32(hdr + 12),
             chunks = ofp_u32(hdr + 20);
    uint64_t blocks = ofp_u32(hdr + 16);
    if (ofp_u16(hdr + 4) != 1 || fh < 28 || ch < 12 || !bs || bs % 4 ||
        fh > (uint64_t)length.QuadPart || chunks > 10000000) {
        ofp_error(error, L"Invalid sparse header.");
        goto done;
    }
    uint64_t pos = fh, total = 0;
    for (unsigned i = 0; i < chunks; i++) {
        if (cancel && InterlockedCompareExchange(cancel, 0, 0)) {
            ofp_error(error, L"Cancelled during sparse validation.");
            goto done;
        }
        unsigned char b[12];
        if (pos > (uint64_t)length.QuadPart || ch > (uint64_t)length.QuadPart - pos ||
            !ofp_read(h, pos, b, 12)) {
            ofp_error(error, L"Truncated sparse chunk.");
            goto done;
        }
        unsigned kind = ofp_u16(b), nblocks = ofp_u32(b + 4), size = ofp_u32(b + 8);
        uint64_t payload;
        if (kind == 0xcac1)
            payload = (uint64_t)nblocks * bs;
        else if (kind == 0xcac2)
            payload = 4;
        else if (kind == 0xcac3)
            payload = 0;
        else if (kind == 0xcac4 && nblocks == 0)
            payload = 4;
        else {
            ofp_error(error, L"Unsupported sparse chunk.");
            goto done;
        }
        if (payload + ch != size || size > (uint64_t)length.QuadPart - pos ||
            total + nblocks > blocks) {
            ofp_error(error, L"Invalid sparse chunk size.");
            goto done;
        }
        pos += size;
        total += nblocks;
    }
    if (total != blocks || pos != (uint64_t)length.QuadPart) {
        ofp_error(error, L"Sparse image totals do not match.");
        goto done;
    }
    *expanded = blocks * bs;
    ok = 1;
done:
    CloseHandle(h);
    return ok;
}
typedef struct {
    char key[40], value[512];
} Field;
typedef struct {
    Field fields[24];
    int count;
} Partition;
static const char *field(Partition *p, const char *name) {
    for (int i = 0; i < p->count; i++)
        if (!strcmp(p->fields[i].key, name))
            return p->fields[i].value;
    return NULL;
}
static int uint_field(const char *s, uint64_t *n) {
    if (!s || !*s || *s == '-' || *s == '+')
        return 0;
    char *end;
    errno = 0;
    *n = strtoull(s, &end, 0);
    return !errno && !*end;
}
static int token(const char *s) {
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_')
            return 0;
    return 1;
}
int ofp_scatter(const wchar_t *dest, OfpArchive *a, volatile LONG *cancel, HANDLE *artifact,
                wchar_t *error) {
    if (a->format != 1)
        return ofp_error(
            error, L"Scatter skipped: only recognized MediaTek eMMC layouts can be converted.");
    OfpEntry *source = NULL;
    for (int i = 0; i < a->count; i++)
        if (a->entries[i].completed && wcsstr(a->entries[i].name, L"Android_scatter.txt")) {
            if (source)
                return ofp_error(error, L"Scatter skipped: multiple source scatters.");
            source = &a->entries[i];
        }
    if (!source || source->size > 2 * 1024 * 1024)
        return ofp_error(
            error,
            L"Scatter skipped: select the original OEM scatter and all its required images.");
    wchar_t *path = calloc(OFP_PATH, sizeof(wchar_t)), *target = calloc(OFP_PATH, sizeof(wchar_t)),
            *temp = calloc(OFP_PATH, sizeof(wchar_t));
    char *text = calloc((size_t)source->size + 1, 1),
         *output = calloc((size_t)source->size * 2 + 2048, 1);
    Partition *parts = calloc(1024, sizeof(Partition));
    HANDLE h = INVALID_HANDLE_VALUE;
    int ok = 0, owned = 0;
    size_t used = 0, capacity = (size_t)source->size * 2 + 2048;
    int count = 0, in_emmc = 0;
    char platform[64] = "", project[64] = "", block[64] = "", channel[64] = "", version[64] = "";
    if (!path || !target || !temp || !text || !output || !parts) {
        ofp_error(error, L"Scatter skipped: not enough memory.");
        goto done;
    }
    ofp_join(path, dest, source->name);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE || !ofp_read(h, 0, text, (DWORD)source->size)) {
        ofp_error(error, L"Scatter skipped: cannot read original.");
        goto done;
    }
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
    char *save = NULL, *line = strtok_s(text, "\r\n", &save);
    while (line) {
        if (InterlockedCompareExchange(cancel, 0, 0)) {
            ofp_error(error, L"Cancelled during scatter validation.");
            goto done;
        }
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '#' || !*line) {
            line = strtok_s(NULL, "\r\n", &save);
            continue;
        }
        if (!strncmp(line, "- storage_type:", 15)) {
            const char *storage = line + 15;
            while (*storage == ' ' || *storage == '\t')
                storage++;
            if (!strcmp(storage, "EMMC"))
                in_emmc = 1;
            else if (in_emmc)
                break;
            line = strtok_s(NULL, "\r\n", &save);
            continue;
        }
        if (!strncmp(line, "- partition_index:", 18) && in_emmc) {
            if (count >= 1024) {
                ofp_error(error, L"Scatter skipped: too many partitions.");
                goto done;
            }
            count++;
        }
        char *colon = strchr(line, ':');
        if (!colon) {
            ofp_error(error, L"Scatter skipped: malformed layout.");
            goto done;
        }
        *colon = 0;
        char *key = line;
        if (!strncmp(key, "- ", 2))
            key += 2;
        char *value = colon + 1;
        while (*value == ' ' || *value == '\t')
            value++;
        size_t n = strlen(value);
        while (n && (value[n - 1] == ' ' || value[n - 1] == '\t'))
            value[--n] = 0;
        if (!strcmp(key, "config_version") && strlen(value) < 64 && !count)
            strcpy(version, value);
        else if (!strcmp(key, "platform") && strlen(value) < 64)
            strcpy(platform, value);
        else if (!strcmp(key, "project") && strlen(value) < 64)
            strcpy(project, value);
        else if (in_emmc && !count && !strcmp(key, "block_size") && strlen(value) < 64)
            strcpy(block, value);
        else if (in_emmc && !count && !strcmp(key, "boot_channel") && strlen(value) < 64)
            strcpy(channel, value);
        else if (in_emmc && count) {
            Partition *p = &parts[count - 1];
            if (p->count >= 24 || strlen(key) >= 40 || strlen(value) >= 512 || field(p, key)) {
                ofp_error(error, L"Scatter skipped: invalid partition field.");
                goto done;
            }
            strcpy(p->fields[p->count].key, key);
            strcpy(p->fields[p->count++].value, value);
        }
        line = strtok_s(NULL, "\r\n", &save);
    }
    uint64_t block_n;
    if (strcmp(version, "V2.1.0") || !count || strncmp(platform, "MT", 2) || !token(platform) ||
        !token(project) || !token(channel) || !uint_field(block, &block_n) || !block_n) {
        ofp_error(error, L"Scatter skipped: incomplete eMMC layout.");
        goto done;
    }
    used = (size_t)snprintf(
        output, capacity,
        "# OFP Studio eMMC compatibility copy; original preserved.\r\n# File validation only; "
        "phone flashing not tested.\r\n- general: MTK_PLATFORM_CFG\r\n  info:\r\n    - "
        "config_version: V1.1.2\r\n      platform: %s\r\n      project: %s\r\n      storage: "
        "EMMC\r\n      boot_channel: %s\r\n      block_size: %s\r\n\r\n",
        platform, project, channel, block);
    const char *required[] = {
        "partition_index",   "partition_name",      "file_name",      "is_download", "type",
        "linear_start_addr", "physical_start_addr", "partition_size", "region",      "storage",
        "boundary_check",    "is_reserved",         "operation_type", "reserve"};
    for (int i = 0; i < count; i++) {
        if (InterlockedCompareExchange(cancel, 0, 0)) {
            ofp_error(error, L"Cancelled during scatter validation.");
            goto done;
        }
        Partition *p = &parts[i];
        for (size_t k = 0; k < sizeof(required) / sizeof(required[0]); k++)
            if (!field(p, required[k])) {
                ofp_error(error, L"Scatter skipped: incomplete partition fields.");
                goto done;
            }
        const char *file = field(p, "file_name"), *name = field(p, "partition_name");
        uint64_t size, start, linear;
        if (!uint_field(field(p, "partition_size"), &size) ||
            !uint_field(field(p, "physical_start_addr"), &start) ||
            !uint_field(field(p, "linear_start_addr"), &linear) || !token(name) ||
            strcmp(field(p, "storage"), "HW_STORAGE_EMMC")) {
            ofp_error(error, L"Scatter skipped: invalid eMMC partition.");
            goto done;
        }
        const char *region = field(p, "region"), *download = field(p, "is_download"),
                   *reserved = field(p, "is_reserved");
        if ((strcmp(region, "EMMC_USER") && strcmp(region, "EMMC_BOOT1_BOOT2") &&
             strcmp(region, "EMMC_BOOT_1") && strcmp(region, "EMMC_BOOT_2") &&
             strcmp(region, "EMMC_RPMB")) ||
            (strcmp(download, "true") && strcmp(download, "false")) ||
            (strcmp(reserved, "true") && strcmp(reserved, "false")) ||
            (!strcmp(download, "true") && !strcmp(file, "NONE")) || !size ||
            size > UINT64_MAX - start || size > UINT64_MAX - linear) {
            ofp_error(error,
                      L"Scatter skipped: unsupported region, download setting or partition span.");
            goto done;
        }
        if (strcmp(file, "NONE")) {
            wchar_t wide[512];
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, file, -1, wide, 512) ||
                !ofp_safe_name(wide)) {
                ofp_error(error, L"Scatter skipped: unsafe image reference.");
                goto done;
            }
            int found = 0;
            for (int j = 0; j < a->count; j++)
                if (a->entries[j].completed && !_wcsicmp(a->entries[j].name, wide))
                    found = 1;
            if (!found) {
                ofp_error(error, L"Scatter skipped: a required image was not extracted: %ls", wide);
                goto done;
            }
            uint64_t expanded;
            ofp_join(path, dest, wide);
            if (!ofp_sparse_size(path, &expanded, cancel, error))
                goto done;
            if (expanded > size) {
                ofp_error(error, L"Scatter skipped: expanded image exceeds partition: %ls", wide);
                goto done;
            }
        }
        for (int k = 0; k < p->count; k++) {
            const char *key = p->fields[k].key, *value = p->fields[k].value;
            if (!strcmp(key, "is_upgradable") || !strcmp(key, "empty_boot_needed"))
                continue;
            if (!strcmp(key, "region") && !strcmp(value, "EMMC_BOOT1_BOOT2"))
                value = "EMMC_BOOT_1";
            if (!strcmp(key, "is_download") && !strcmp(name, "preloader"))
                value = "false";
            int wrote = snprintf(output + used, capacity - used, "%s%s: %s\r\n",
                                 !strcmp(key, "partition_index") ? "- " : "  ", key, value);
            if (wrote < 0 || (size_t)wrote >= capacity - used) {
                ofp_error(error, L"Scatter skipped: output too large.");
                goto done;
            }
            used += (size_t)wrote;
        }
        if (used + 2 >= capacity)
            goto done;
        memcpy(output + used, "\r\n", 2);
        used += 2;
    }
    wchar_t filename[128];
    swprintf(filename, 128, L"%hs_Android_scatter_SPFT_EMMC.txt", platform);
    ofp_join(target, dest, filename);
    swprintf(temp, OFP_PATH, L"%ls.ofp-studio.partial", target);
    if (InterlockedCompareExchange(cancel, 0, 0)) {
        ofp_error(error, L"Cancelled before scatter publication.");
        goto done;
    }
    h = CreateFileW(temp, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        ofp_error(error, L"Scatter skipped: output exists or is inaccessible.");
        goto done;
    }
    owned = 1;
    DWORD written;
    if (!WriteFile(h, output, (DWORD)used, &written, NULL) || written != used ||
        !FlushFileBuffers(h)) {
        ofp_error(error, L"Scatter skipped: write failed.");
        goto done;
    }
    if (!ofp_finalize_output(h, target, error)) {
        ofp_error(error, L"Scatter skipped: cannot finalize copy.");
        goto done;
    }
    owned = 0;
    *artifact = h;
    h = INVALID_HANDLE_VALUE;
    ok = 1;
done:
    if (owned && h != INVALID_HANDLE_VALUE)
        ofp_discard_output(h);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    free(path);
    free(target);
    free(temp);
    free(text);
    free(output);
    free(parts);
    return ok;
}
