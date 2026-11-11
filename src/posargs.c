#define _GNU_SOURCE
#include "yaitso.h"

const char *posarg_get(struct yaitso_ctx *c, int idx)
{
    if (!c || idx < 0 || idx >= c->posargc) return NULL;
    return c->posargs[idx];
}
