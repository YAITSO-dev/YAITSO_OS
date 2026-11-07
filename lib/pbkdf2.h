#ifndef LIBUSER_H
#define LIBUSER_H

#include <stddef.h>

void pbkdf2_sha256(const char *password, const char *salt,
                   int iterations, unsigned char *out, size_t outlen);

#endif
