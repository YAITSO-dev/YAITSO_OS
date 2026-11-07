#define _GNU_SOURCE
#include "yaitso.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

struct cmd {
    const char *name;
    int (*fn)(struct yaitso_ctx *, int, char **);
};

static const struct cmd g_cmds[] = {
    {"help",      builtin_help},
    {"version",   builtin_version},
    {"about",     builtin_about},
    {"welcome",   builtin_welcome},
    {"clear",     builtin_clear},
    {"reset",     builtin_reset},
    {"echo",      builtin_echo},
    {"printf",    builtin_printf},
    {"true",      builtin_true},
    {"false",     builtin_false},
    {"date",      builtin_date},
    {"uname",     builtin_uname},
    {"whoami",    builtin_whoami},
    {"history",   builtin_history},
    {"sleep",     builtin_sleep},
    {"time",      builtin_time},
    {"set",       builtin_set},
    {"export",    builtin_export},
    {"unset",     builtin_unset},
    {"env",       builtin_env},
    {"alias",     builtin_alias},
    {"unalias",   builtin_unalias},
    {"which",     builtin_which},
    {"type",      builtin_type},
    {"jobs",      builtin_jobs},
    {"fg",        builtin_fg},
    {"bg",        builtin_bg},
    {"wait",      builtin_wait},
    {"shift",     builtin_shift},
    {"cd",        builtin_cd},
    {"pwd",       builtin_pwd},
    {"ls",        builtin_ls},
    {"cat",       builtin_cat},
    {"head",      builtin_head},
    {"tail",      builtin_tail},
    {"wc",        builtin_wc},
    {"grep",      builtin_grep},
    {"seq",       builtin_seq},
    {"pushd",     builtin_pushd},
    {"popd",      builtin_popd},
    {"dirs",      builtin_dirs},
    {"source",    builtin_source},
    {".",         builtin_source},
    {"read",      builtin_read},
    {"exit",      builtin_exit},
    {"logout",    builtin_logout},
    {"bye",       builtin_logout},
    {NULL, NULL}
};

int commands_run(struct yaitso_ctx *c, int argc, char **argv)
{
    if (argc < 1 || !argv[0]) return -1;
    for (const struct cmd *p = g_cmds; p->name; p++) {
        if (!strcmp(p->name, argv[0])) {
            int rc = p->fn(c, argc, argv);
            c->last_status = rc;
            return 0;
        }
    }
    return -1;
}
