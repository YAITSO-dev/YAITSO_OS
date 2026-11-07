#define _GNU_SOURCE
#include "login.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crypt.h>

/* Файл теней; переопределяется при сборке для тестов (-DSHADOW_FILE=...). */
#ifndef SHADOW_FILE
#define SHADOW_FILE "/etc/shadow"
#endif

/* Имя входа: [a-z_][a-z0-9_-]*, до 32 символов. */
int valid_login_name(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 32) return 0;
    if (!((s[0] >= 'a' && s[0] <= 'z') || s[0] == '_')) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

/* Хеш пользователя из /etc/shadow. Возвращает 0 и заполняет hash, иначе -1. */
static int lookup_shadow(const char *user, char *hash, size_t n)
{
    FILE *f = fopen(SHADOW_FILE, "r");
    if (!f) return -1;
    char line[512];
    int found = -1;
    size_t ulen = strlen(user);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *colon = strchr(line, ':');
        if (!colon || (size_t)(colon - line) != ulen) continue;
        if (strncmp(line, user, ulen) != 0) continue;
        char *h = colon + 1;
        char *end = strchr(h, ':');
        size_t hl = end ? (size_t)(end - h) : strlen(h);
        if (hl >= n) break;
        memcpy(hash, h, hl);
        hash[hl] = 0;
        found = 0;
        break;
    }
    fclose(f);
    return found;
}

/* Сравнение без раннего выхода по содержимому. */
static int ct_equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (la != lb);
    size_t n = la > lb ? la : lb;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = i < la ? (unsigned char)a[i] : 0;
        unsigned char cb = i < lb ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

/* 0 — пароль верный. Любая ошибка (нет пользователя, заблокирован, пустой пароль) — -1. */
int crypt_check(const char *user, const char *pass)
{
    if (!valid_login_name(user) || !pass) return -1;
    char stored[256];
    if (lookup_shadow(user, stored, sizeof(stored)) != 0) return -1;
    /* '!' и '*' — заблокированная учётная запись, пустое поле — пароля нет. */
    if (stored[0] == '\0' || stored[0] == '!' || stored[0] == '*') return -1;
    struct crypt_data cd;
    memset(&cd, 0, sizeof(cd));
    char *calc = crypt_r(pass, stored, &cd);
    if (!calc) return -1;
    int ok = ct_equal(calc, stored);
    memset(&cd, 0, sizeof(cd));
    return ok ? 0 : -1;
}
