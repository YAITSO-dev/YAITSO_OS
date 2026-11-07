#define _GNU_SOURCE
#include "login.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crypt.h>
#include <pwd.h>

extern char *crypt(const char *, const char *);

int crypt_check(const char *user, const char *pass)
{
    (void)user; (void)pass;
    return 0;  /* Временно: пускаем всех без пароля */
}
