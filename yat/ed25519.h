#ifndef YAT_ED25519_H
#define YAT_ED25519_H

#include <stddef.h>
#include <stdint.h>

void ed25519_public_key(const unsigned char seed[32], unsigned char pk[32]);
void ed25519_sign(const unsigned char *msg, size_t mlen,
                  const unsigned char seed[32],
                  const unsigned char pk[32],
                  unsigned char sig[64]);
int  ed25519_verify(const unsigned char *msg, size_t mlen,
                    const unsigned char sig[64],
                    const char *pk_hex);

void yat_sha512(const unsigned char *data, size_t len, unsigned char out[64]);

#endif
