#ifndef OFP_H
#define OFP_H
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <bcrypt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define OFP_MAX_ENTRIES 8192
#define OFP_PATH 32768
#define OFP_ERROR 1024
#define OFP_VERSION L"1.1.0"
typedef struct {
    wchar_t name[512], label[128];
    uint64_t offset, size, encrypted;
    char vendor_sha[65], vendor_md5[33], sha256[65];
    int selected, completed;
    wchar_t *aliases;
} OfpEntry;
typedef struct {
    wchar_t path[OFP_PATH], identity[256];
    uint64_t source_size, total;
    BY_HANDLE_FILE_INFORMATION source_info;
    int format, count, logical_count;
    unsigned char key[16], iv[16];
    OfpEntry *entries;
    unsigned char *xml;
    size_t xml_size;
    volatile LONG *cancel;
} OfpArchive;
typedef struct {
    int manifest, verify, scatter;
} OfpOptions;
typedef void (*OfpProgress)(void *, int, uint64_t, uint64_t, const wchar_t *);
typedef struct {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
} OfpHash;
typedef struct {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_KEY_HANDLE key;
    unsigned char iv[16], pad[16];
    unsigned used;
} OfpCipher;
int ofp_error(wchar_t *, const wchar_t *, ...);
int ofp_hash_begin(OfpHash *, const wchar_t *);
int ofp_hash_add(OfpHash *, const void *, size_t);
int ofp_hash_end(OfpHash *, char *, size_t);
void ofp_hash_close(OfpHash *);
int ofp_digest(const wchar_t *, const void *, size_t, char *, size_t);
int ofp_cipher_begin(OfpCipher *, const unsigned char *, const unsigned char *);
int ofp_cipher_decrypt(OfpCipher *, unsigned char *, size_t);
void ofp_cipher_close(OfpCipher *);
int ofp_derive(const char *, const char *, const char *, unsigned char *, unsigned char *);
uint16_t ofp_u16(const unsigned char *);
uint32_t ofp_u32(const unsigned char *);
uint64_t ofp_u64(const unsigned char *);
int ofp_read(HANDLE, uint64_t, void *, DWORD);
int ofp_safe_name(const wchar_t *);
int ofp_add_entry(OfpArchive *, OfpEntry *, HANDLE, wchar_t *);
int ofp_inspect(const wchar_t *, OfpArchive *, wchar_t *);
int ofp_inspect_cancel(const wchar_t *, OfpArchive *, volatile LONG *, wchar_t *);
int ofp_mtk(HANDLE, OfpArchive *, wchar_t *);
int ofp_qc(HANDLE, OfpArchive *, wchar_t *);
void ofp_free(OfpArchive *);
int ofp_extract(OfpArchive *, const wchar_t *, const OfpOptions *, OfpProgress, void *,
                volatile LONG *, wchar_t *);
int ofp_scatter(const wchar_t *, OfpArchive *, volatile LONG *, HANDLE *, wchar_t *);
int ofp_sparse_size(const wchar_t *, uint64_t *, volatile LONG *, wchar_t *);
int ofp_join(wchar_t *, const wchar_t *, const wchar_t *);
int ofp_finalize_output(HANDLE, const wchar_t *, wchar_t *);
void ofp_discard_output(HANDLE);
typedef struct {
    HANDLE *handles;
    size_t count, cap;
} OfpDirLocks;
int ofp_prepare_dirs(OfpDirLocks *, const wchar_t *, int, int, wchar_t *);
void ofp_unlock_dirs(OfpDirLocks *);
#endif
