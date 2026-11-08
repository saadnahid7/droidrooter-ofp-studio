#include "ofp.h"
#include <string.h>
void ofp_hash_close(OfpHash *h) {
    if (h->hash)
        BCryptDestroyHash(h->hash);
    if (h->alg)
        BCryptCloseAlgorithmProvider(h->alg, 0);
    memset(h, 0, sizeof(*h));
}
int ofp_hash_begin(OfpHash *h, const wchar_t *name) {
    memset(h, 0, sizeof(*h));
    if (BCryptOpenAlgorithmProvider(&h->alg, name, NULL, 0) < 0 ||
        BCryptCreateHash(h->alg, &h->hash, NULL, 0, NULL, 0, 0) < 0) {
        ofp_hash_close(h);
        return 0;
    }
    return 1;
}
int ofp_hash_add(OfpHash *h, const void *p, size_t n) {
    return BCryptHashData(h->hash, (PUCHAR)p, (ULONG)n, 0) >= 0;
}
int ofp_hash_end(OfpHash *h, char *hex, size_t n) {
    unsigned char bytes[32];
    int ok = n <= 32 && BCryptFinishHash(h->hash, bytes, (ULONG)n, 0) >= 0;
    if (ok) {
        static const char digits[] = "0123456789abcdef";
        for (size_t i = 0; i < n; i++) {
            hex[2 * i] = digits[bytes[i] >> 4];
            hex[2 * i + 1] = digits[bytes[i] & 15];
        }
        hex[n * 2] = 0;
    }
    ofp_hash_close(h);
    return ok;
}
int ofp_digest(const wchar_t *name, const void *p, size_t n, char *hex, size_t bytes) {
    OfpHash h = {0};
    if (!ofp_hash_begin(&h, name))
        return 0;
    int ok = ofp_hash_add(&h, p, n);
    if (!ok) {
        ofp_hash_close(&h);
        return 0;
    }
    return ofp_hash_end(&h, hex, bytes);
}
void ofp_cipher_close(OfpCipher *c) {
    if (c->key)
        BCryptDestroyKey(c->key);
    if (c->alg)
        BCryptCloseAlgorithmProvider(c->alg, 0);
    SecureZeroMemory(c, sizeof(*c));
}
int ofp_cipher_begin(OfpCipher *c, const unsigned char *key, const unsigned char *iv) {
    memset(c, 0, sizeof(*c));
    memcpy(c->iv, iv, 16);
    c->used = 16;
    if (BCryptOpenAlgorithmProvider(&c->alg, BCRYPT_AES_ALGORITHM, NULL, 0) < 0 ||
        BCryptSetProperty(c->alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                          sizeof(BCRYPT_CHAIN_MODE_ECB), 0) < 0 ||
        BCryptGenerateSymmetricKey(c->alg, &c->key, NULL, 0, (PUCHAR)key, 16, 0) < 0) {
        ofp_cipher_close(c);
        return 0;
    }
    return 1;
}
int ofp_cipher_decrypt(OfpCipher *c, unsigned char *p, size_t n) {
    /* CFB128: encrypt the previous ciphertext block using Windows AES ECB.
       Preserve feedback across arbitrary chunk boundaries and partial blocks. */
    for (size_t i = 0; i < n; i++) {
        if (c->used == 16) {
            ULONG written = 0;
            if (BCryptEncrypt(c->key, c->iv, 16, NULL, NULL, 0, c->pad, 16, &written, 0) < 0 ||
                written != 16)
                return 0;
            c->used = 0;
        }
        unsigned char b = p[i];
        p[i] ^= c->pad[c->used];
        c->iv[c->used++] = b;
    }
    return 1;
}
static int hexval(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
static int unhex(const char *s, unsigned char *p) {
    for (int i = 0; i < 16; i++) {
        int a = hexval(s[i * 2]), b = hexval(s[i * 2 + 1]);
        if (a < 0 || b < 0)
            return 0;
        p[i] = (unsigned char)((a << 4) | b);
    }
    return 1;
}
int ofp_derive(const char *mask, const char *k, const char *v, unsigned char *key,
               unsigned char *iv) {
    unsigned char m[16], kb[16], vb[16];
    char hex[33];
    if (!unhex(mask, m) || !unhex(k, kb) || !unhex(v, vb))
        return 0;
    for (int i = 0; i < 16; i++) {
        unsigned x = kb[i] ^ m[i], y = vb[i] ^ m[i];
        kb[i] = (unsigned char)((x >> 4) | (x << 4));
        vb[i] = (unsigned char)((y >> 4) | (y << 4));
    }
    if (!ofp_digest(BCRYPT_MD5_ALGORITHM, kb, 16, hex, 16))
        return 0;
    memcpy(key, hex, 16);
    if (!ofp_digest(BCRYPT_MD5_ALGORITHM, vb, 16, hex, 16))
        return 0;
    memcpy(iv, hex, 16);
    return 1;
}
