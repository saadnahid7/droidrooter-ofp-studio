/* OFP metadata/key profiles adapted from Bjoern Kerler's MIT-licensed oppo_decrypt.
   See THIRD_PARTY_NOTICES.md. */
#define OFP_MTK_KEYS
#include "keys.h"
#include "ofp.h"
#include <stdlib.h>
#include <string.h>
static void unshuffle(unsigned char *p, size_t n) {
    const char *key = "geyixue";
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)(key[i % 7] ^ ((p[i] >> 4) | (p[i] << 4)));
}
static int text(wchar_t *out, int cap, const unsigned char *p, int n) {
    char buf[512];
    if (n >= 512)
        return 0;
    memcpy(buf, p, n);
    buf[n] = 0;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, -1, out, cap) > 0;
}
int ofp_mtk(HANDLE h, OfpArchive *a, wchar_t *error) {
    unsigned char first[16], trial[16], header[108];
    int found = 0;
    if (!ofp_read(h, 0, first, 16))
        return ofp_error(error, L"Cannot read archive.");
    for (size_t i = 0; i < sizeof(mtk_keys) / sizeof(mtk_keys[0]); i++) {
        if (mtk_keys[i][2]) {
            if (!ofp_derive(mtk_keys[i][0], mtk_keys[i][1], mtk_keys[i][2], a->key, a->iv))
                continue;
        } else {
            memcpy(a->key, mtk_keys[i][0], 16);
            memcpy(a->iv, mtk_keys[i][1], 16);
        }
        OfpCipher cipher = {0};
        memcpy(trial, first, 16);
        int ok = ofp_cipher_begin(&cipher, a->key, a->iv) && ofp_cipher_decrypt(&cipher, trial, 16);
        ofp_cipher_close(&cipher);
        if (ok && !memcmp(trial, "MMM", 3)) {
            found = 1;
            break;
        }
    }
    if (!found)
        return ofp_error(error, L"Unsupported OFP format or unknown MediaTek key. ZIP and OnePlus "
                                L"OPS are not supported.");
    if (!ofp_read(h, a->source_size - 108, header, 108))
        return ofp_error(error, L"Cannot read MTK footer.");
    unshuffle(header, 108);
    unsigned count = ofp_u16(header + 72);
    uint64_t table_size = (uint64_t)count * 96;
    if (!count || count > OFP_MAX_ENTRIES || table_size > a->source_size - 108)
        return ofp_error(error, L"Invalid MTK file table.");
    unsigned char *table = malloc((size_t)table_size);
    if (!table)
        return ofp_error(error, L"Not enough memory.");
    uint64_t table_at = a->source_size - 108 - table_size;
    if (!ofp_read(h, table_at, table, (DWORD)table_size)) {
        free(table);
        return ofp_error(error, L"Cannot read MTK file table.");
    }
    unshuffle(table, (size_t)table_size);
    wchar_t project[128] = {0}, cpu[32] = {0};
    text(project, 128, header + 74, 32);
    text(cpu, 32, header + 60, 7);
    swprintf(a->identity, 256, L"MediaTek MT%ls  |  Project %ls", cpu, project);
    a->format = 1;
    int ok = 1;
    for (unsigned i = 0; i < count; i++) {
        unsigned char *p = table + i * 96;
        OfpEntry e = {0};
        if (!text(e.name, 512, p + 56, 32) || !text(e.label, 128, p, 32)) {
            ok = ofp_error(error, L"Invalid UTF-8 filename in OFP.");
            break;
        }
        e.offset = ofp_u64(p + 32);
        e.size = ofp_u64(p + 40);
        e.encrypted = ofp_u64(p + 48);
        if (e.offset > table_at || e.size > table_at - e.offset) {
            ok = ofp_error(error, L"Image overlaps MTK metadata: %ls", e.name);
            break;
        }
        if (!ofp_add_entry(a, &e, h, error)) {
            ok = 0;
            break;
        }
    }
    free(table);
    return ok;
}
