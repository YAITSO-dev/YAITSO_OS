#ifndef LIBUSER_MAIN_H
#define LIBUSER_MAIN_H

#include <stddef.h>

void pbkdf2_sha256(const char *password, const char *salt,
                   int iterations, unsigned char *out, size_t outlen);

void chacha20_xor(const unsigned char key[32], const unsigned char nonce[12],
                  const unsigned char *in, unsigned char *out, size_t len);

#endif
