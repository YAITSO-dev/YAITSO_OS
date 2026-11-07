/*
 * Ed25519 и SHA-512 для yat через OpenSSL (libcrypto, уже нужна для TLS).
 *
 * Прежняя собственная реализация не проходила тестовые векторы RFC 8032
 * (неверный публичный ключ из seed, неверные подписи), поэтому заменена.
 * Заголовки OpenSSL не требуются: нужные функции объявлены здесь вручную.
 */
#include "ed25519.h"
#include <string.h>

typedef struct evp_pkey_st EVP_PKEY;
typedef struct evp_pkey_ctx_st EVP_PKEY_CTX;
typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct evp_md_st EVP_MD;

#define YAT_NID_ED25519 1087

EVP_PKEY *EVP_PKEY_new_raw_private_key(int type, void *e, const unsigned char *key, size_t keylen);
EVP_PKEY *EVP_PKEY_new_raw_public_key(int type, void *e, const unsigned char *key, size_t keylen);
int       EVP_PKEY_get_raw_public_key(const EVP_PKEY *pkey, unsigned char *pub, size_t *len);
void      EVP_PKEY_free(EVP_PKEY *pkey);
EVP_MD_CTX *EVP_MD_CTX_new(void);
void      EVP_MD_CTX_free(EVP_MD_CTX *ctx);
int       EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, void *e, EVP_PKEY *pkey);
int       EVP_DigestSign(EVP_MD_CTX *ctx, unsigned char *sig, size_t *siglen, const unsigned char *tbs, size_t tbslen);
int       EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type, void *e, EVP_PKEY *pkey);
int       EVP_DigestVerify(EVP_MD_CTX *ctx, const unsigned char *sig, size_t siglen, const unsigned char *tbs, size_t tbslen);
const EVP_MD *EVP_sha512(void);
int       EVP_Digest(const void *data, size_t count, unsigned char *md, unsigned int *size, const EVP_MD *type, void *impl);

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 0x20);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void ed25519_public_key(const unsigned char seed[32], unsigned char pk[32])
{
    EVP_PKEY *sk = EVP_PKEY_new_raw_private_key(YAT_NID_ED25519, NULL, seed, 32);
    if (!sk) { memset(pk, 0, 32); return; }
    size_t len = 32;
    if (EVP_PKEY_get_raw_public_key(sk, pk, &len) != 1) memset(pk, 0, 32);
    EVP_PKEY_free(sk);
}

void ed25519_sign(const unsigned char *msg, size_t mlen,
                  const unsigned char seed[32],
                  const unsigned char pk[32],
                  unsigned char sig[64])
{
    (void)pk; /* публичный ключ выводится из seed */
    memset(sig, 0, 64);
    EVP_PKEY *sk = EVP_PKEY_new_raw_private_key(YAT_NID_ED25519, NULL, seed, 32);
    if (!sk) return;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t siglen = 64;
    if (ctx && EVP_DigestSignInit(ctx, NULL, NULL, NULL, sk) == 1)
        EVP_DigestSign(ctx, sig, &siglen, msg, mlen);
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(sk);
}

/* 0 — подпись верна, -1 — нет (включая неверный формат ключа). */
int ed25519_verify(const unsigned char *msg, size_t mlen,
                   const unsigned char sig[64],
                   const char *pk_hex)
{
    if (!pk_hex || strlen(pk_hex) < 64) return -1;
    unsigned char pk[32];
    for (int i = 0; i < 32; i++) {
        int hi = hexval(pk_hex[2 * i]), lo = hexval(pk_hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        pk[i] = (unsigned char)(hi * 16 + lo);
    }
    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(YAT_NID_ED25519, NULL, pk, 32);
    if (!pkey) return -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    int ok = -1;
    if (ctx && EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1)
        ok = (EVP_DigestVerify(ctx, sig, 64, msg, mlen) == 1) ? 0 : -1;
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

void yat_sha512(const unsigned char *data, size_t len, unsigned char out[64])
{
    unsigned int n = 0;
    EVP_Digest(data, len, out, &n, EVP_sha512(), NULL);
}
