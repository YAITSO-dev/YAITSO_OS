#include "libuser.h"
#include <string.h>
#include <stdint.h>

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void chacha20_quarter(uint32_t st[16], int a, int b, int c, int d)
{
    st[a] += st[b]; st[d] ^= st[a]; st[d] = rotl32(st[d], 16);
    st[c] += st[d]; st[b] ^= st[c]; st[b] = rotl32(st[b], 12);
    st[a] += st[b]; st[d] ^= st[a]; st[d] = rotl32(st[d], 8);
    st[c] += st[d]; st[b] ^= st[c]; st[b] = rotl32(st[b], 7);
}

static void chacha20_block(const uint32_t in[16], uint32_t out[16])
{
    uint32_t x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        chacha20_quarter(x, 0, 4,  8, 12);
        chacha20_quarter(x, 1, 5,  9, 13);
        chacha20_quarter(x, 2, 6, 10, 14);
        chacha20_quarter(x, 3, 7, 11, 15);
        chacha20_quarter(x, 0, 5, 10, 15);
        chacha20_quarter(x, 1, 6, 11, 12);
        chacha20_quarter(x, 2, 7,  8, 13);
        chacha20_quarter(x, 3, 4,  9, 14);
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + in[i];
}

void chacha20_xor(const unsigned char key[32], const unsigned char nonce[12],
                  const unsigned char *in, unsigned char *out, size_t len)
{
    uint32_t st[16];
    st[0] = 0x61707865;
    st[1] = 0x3320646e;
    st[2] = 0x79622d32;
    st[3] = 0x6b206574;

    for (int i = 0; i < 8; i++)
        st[4 + i] = (uint32_t)key[i*4] | ((uint32_t)key[i*4+1] << 8) |
                    ((uint32_t)key[i*4+2] << 16) | ((uint32_t)key[i*4+3] << 24);

    st[12] = 0;
    for (int i = 0; i < 3; i++)
        st[13 + i] = (uint32_t)nonce[i*4] | ((uint32_t)nonce[i*4+1] << 8) |
                     ((uint32_t)nonce[i*4+2] << 16) | ((uint32_t)nonce[i*4+3] << 24);

    size_t offset = 0;
    while (offset < len) {
        uint32_t block[16];
        chacha20_block(st, block);

        unsigned char *ks = (unsigned char *)block;
        size_t chunk = len - offset;
        if (chunk > 64) chunk = 64;
        for (size_t i = 0; i < chunk; i++)
            out[offset + i] = in[offset + i] ^ ks[i];
        offset += chunk;
        st[12]++;
    }
}
