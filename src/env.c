#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>

const char *env_get(struct yaitso_ctx *c, const char *k)
{
    if (!c || !k) return NULL;
    for (int i = 0; i < c->env_count; i++)
        if (!strcmp(c->env[i].key, k)) return c->env[i].value;
    return NULL;
}

int env_set(struct yaitso_ctx *c, const char *k, const char *v)
{
    if (!c || !k) return -1;
    if (c->env_count == c->env_cap) {
        c->env_cap = c->env_cap ? c->env_cap * 2 : 32;
        c->env = xrealloc(c->env, sizeof(*c->env) * (size_t)c->env_cap);
    }
    for (int i = 0; i < c->env_count; i++) {
        if (!strcmp(c->env[i].key, k)) {
            xfree(c->env[i].value);
            c->env[i].value = xstrdup(v ? v : "");
            setenv(k, c->env[i].value, 1);
            return 0;
        }
    }
    c->env[c->env_count].key   = xstrdup(k);
    c->env[c->env_count].value = xstrdup(v ? v : "");
    setenv(k, c->env[c->env_count].value, 1);
    c->env_count++;
    return 0;
}

int env_unset(struct yaitso_ctx *c, const char *k)
{
    if (!c || !k) return -1;
    for (int i = 0; i < c->env_count; i++) {
        if (!strcmp(c->env[i].key, k)) {
            xfree(c->env[i].key);
            xfree(c->env[i].value);
            for (int j = i; j < c->env_count - 1; j++) c->env[j] = c->env[j + 1];
            c->env_count--;
            unsetenv(k);
            return 0;
        }
    }
    return 1;
}

char **env_array(struct yaitso_ctx *c)
{
    char **arr = xmalloc(sizeof(char *) * (size_t)(c->env_count + 1));
    for (int i = 0; i < c->env_count; i++) {
        size_t kl = strlen(c->env[i].key);
        size_t vl = strlen(c->env[i].value);
        char *s = xmalloc(kl + vl + 2);
        memcpy(s, c->env[i].key, kl);
        s[kl] = '=';
        memcpy(s + kl + 1, c->env[i].value, vl);
        s[kl + vl + 1] = 0;
        arr[i] = s;
    }
    arr[c->env_count] = NULL;
    return arr;
}

void env_free_array(char **arr)
{
    if (!arr) return;
    for (int i = 0; arr[i]; i++) xfree(arr[i]);
    xfree(arr);
}

const char *alias_get(struct yaitso_ctx *c, const char *name)
{
    if (!c || !name) return NULL;
    for (int i = 0; i < c->alias_count; i++)
        if (!strcmp(c->aliases[i].name, name)) return c->aliases[i].value;
    return NULL;
}

int alias_set(struct yaitso_ctx *c, const char *name, const char *value)
{
    if (!c || !name || !value) return -1;
    for (int i = 0; i < c->alias_count; i++) {
        if (!strcmp(c->aliases[i].name, name)) {
            xfree(c->aliases[i].value);
            c->aliases[i].value = xstrdup(value);
            return 0;
        }
    }
    if (c->alias_count == c->alias_cap) {
        c->alias_cap = c->alias_cap ? c->alias_cap * 2 : 16;
        c->aliases = xrealloc(c->aliases,
                              sizeof(*c->aliases) * (size_t)c->alias_cap);
    }
    c->aliases[c->alias_count].name  = xstrdup(name);
    c->aliases[c->alias_count].value = xstrdup(value);
    c->alias_count++;
    return 0;
}

int alias_unset(struct yaitso_ctx *c, const char *name)
{
    if (!c || !name) return -1;
    for (int i = 0; i < c->alias_count; i++) {
        if (!strcmp(c->aliases[i].name, name)) {
            xfree(c->aliases[i].name);
            xfree(c->aliases[i].value);
            for (int j = i; j < c->alias_count - 1; j++)
                c->aliases[j] = c->aliases[j + 1];
            c->alias_count--;
            return 0;
        }
    }
    return 1;
}
