#include "libuser.h"
#include <string.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    unsigned char buf[64];
} sha256_ctx;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(uint32_t state[8], const unsigned char block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
    c->state[0] = 0x6a09e667;
    c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372;
    c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f;
    c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab;
    c->state[7] = 0x5be0cd19;
    c->count = 0;
}

static void sha256_update(sha256_ctx *c, const unsigned char *data, size_t len)
{
    size_t have = c->count % 64;
    size_t need = 64 - have;

    c->count += len;

    if (have && len >= need) {
        memcpy(c->buf + have, data, need);
        sha256_transform(c->state, c->buf);
        data += need;
        len  -= need;
        have = 0;
    }
    while (len >= 64) {
        sha256_transform(c->state, data);
        data += 64;
        len  -= 64;
    }
    if (len) memcpy(c->buf + have, data, len);
}

static void sha256_final(sha256_ctx *c, unsigned char out[32])
{
    size_t have = c->count % 64;
    c->buf[have++] = 0x80;
    if (have > 56) {
        memset(c->buf + have, 0, 64 - have);
        sha256_transform(c->state, c->buf);
        have = 0;
    }
    memset(c->buf + have, 0, 56 - have);

    uint64_t bits = c->count * 8;
    for (int i = 0; i < 8; i++)
        c->buf[56 + i] = (unsigned char)(bits >> (56 - 8 * i));

    sha256_transform(c->state, c->buf);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(c->state[i] >> 24);
        out[i*4+1] = (unsigned char)(c->state[i] >> 16);
        out[i*4+2] = (unsigned char)(c->state[i] >> 8);
        out[i*4+3] = (unsigned char)(c->state[i]);
    }
}

static void hmac_sha256(const unsigned char *key, size_t klen,
                        const unsigned char *data, size_t dlen,
                        unsigned char out[32])
{
    unsigned char k[64];
    memset(k, 0, sizeof(k));
    if (klen > 64) {
        sha256_ctx c;
        sha256_init(&c);
        sha256_update(&c, key, klen);
        sha256_final(&c, k);
    } else {
        memcpy(k, key, klen);
    }

    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    sha256_ctx c;
    unsigned char inner[32];
    sha256_init(&c);
    sha256_update(&c, ipad, 64);
    sha256_update(&c, data, dlen);
    sha256_final(&c, inner);

    sha256_init(&c);
    sha256_update(&c, opad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

void pbkdf2_sha256(const char *password, const char *salt,
                   int iterations, unsigned char *out, size_t outlen)
{
    size_t plen = strlen(password);
    size_t slen = strlen(salt);
    unsigned char buf[64 + 4];

    int block = 1;
    size_t offset = 0;

    while (offset < outlen) {
        memcpy(buf, salt, slen);
        buf[slen]   = (unsigned char)(block >> 24);
        buf[slen+1] = (unsigned char)(block >> 16);
        buf[slen+2] = (unsigned char)(block >> 8);
        buf[slen+3] = (unsigned char)(block);

        unsigned char u[32];
        hmac_sha256((const unsigned char *)password, plen, buf, slen + 4, u);

        unsigned char t[32];
        memcpy(t, u, 32);

        for (int i = 1; i < iterations; i++) {
            hmac_sha256((const unsigned char *)password, plen, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }

        size_t copy = outlen - offset;
        if (copy > 32) copy = 32;
        memcpy(out + offset, t, copy);
        offset += copy;
        block++;
    }
}
