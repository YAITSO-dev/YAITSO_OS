#define _GNU_SOURCE
#include "login.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crypt.h>
#include <pwd.h>
#include <shadow.h>

extern char *crypt(const char *, const char *);

int crypt_check(const char *user, const char *pass)
{
    struct spwd *sp = getspnam(user);
    const char *hash = sp ? sp->sp_pwdp : NULL;

    /* Нет записи, пустой/заблокированный хеш — вход запрещён. */
    if (!hash || !*hash || hash[0] == '!' || hash[0] == '*') return -1;

    char *h = crypt(pass, hash);
    if (!h) return -1;
    return strcmp(h, hash) == 0 ? 0 : -1;
}
