#ifndef YAITSO_H
#define YAITSO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define YAITSO_HISTORY_DEFAULT 1000
#define YAITSO_PROMPT_DEFAULT  "\\u@\\h:\\w\\$ "
#define YAITSO_MAX_JOBS        64
#define YAITSO_MAX_ALIASES     128

struct yaitso_env {
    char *key;
    char *value;
};

struct yaitso_alias {
    char *name;
    char *value;
};

struct yaitso_ctx {
    char  *prompt;
    int    histsize;
    char  *histfile;
    char **history;
    int    hist_count;
    int    hist_cap;
    int    hist_pos;

    struct yaitso_env  *env;
    int                 env_count;
    int                 env_cap;

    struct yaitso_alias *aliases;
    int                  alias_count;
    int                  alias_cap;

    char **posargs;              /* $0, $1, ... */
    int    posargc;

    int    interactive;
    int    last_status;
    pid_t  last_bg_pid;
    char  *cwd_prev;
    int    should_exit;
    int    exit_code;
    int    lineno;
};

int  yaitso_main(int argc, char **argv);

int  shell_run(struct yaitso_ctx *c);

char *readline_yaitso(struct yaitso_ctx *c, const char *prompt);

void  history_init(struct yaitso_ctx *c, int size, const char *path);
void  history_free(struct yaitso_ctx *c);
void  history_add(struct yaitso_ctx *c, const char *line);
void  history_save(struct yaitso_ctx *c);
void  history_clear(struct yaitso_ctx *c);
const char *history_get(struct yaitso_ctx *c, int idx);
int   history_count(void);

int  config_load(struct yaitso_ctx *c);
int  config_load_file(struct yaitso_ctx *c, const char *path);
int  config_load_dir(struct yaitso_ctx *c, const char *dir);

int  commands_run(struct yaitso_ctx *c, int argc, char **argv);

int  builtin_cd(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_pwd(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_echo(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_printf(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_true(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_false(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_help(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_version(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_about(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_welcome(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_clear(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_reset(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_date(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_uname(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_whoami(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_history(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_sleep(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_time(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_set(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_export(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_unset(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_env(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_alias(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_unalias(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_which(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_type(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_jobs(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_fg(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_bg(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_wait(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_shift(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_ls(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_cat(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_head(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_tail(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_wc(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_grep(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_seq(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_pushd(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_popd(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_dirs(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_source(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_read(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_exit(struct yaitso_ctx *c, int argc, char **argv);
int  builtin_logout(struct yaitso_ctx *c, int argc, char **argv);

int  exec_line(struct yaitso_ctx *c, const char *line);
int  exec_pipeline(struct yaitso_ctx *c, char *const *stages, int nstages,
                   const char *infile, const char *outfile, int append,
                   const char *errfile, int errappend, int bg);

void jobs_init(void);
int  jobs_add(struct yaitso_ctx *c, pid_t pid, const char *cmd);
int  jobs_count(void);
int  jobs_fg(struct yaitso_ctx *c, int id);
int  jobs_bg(struct yaitso_ctx *c, int id);
int  jobs_wait_all(struct yaitso_ctx *c);
void jobs_reap(struct yaitso_ctx *c);
void jobs_print(struct yaitso_ctx *c);

int  glob_expand(const char *pattern, char ***out, int *outn);

char *expand_vars(struct yaitso_ctx *c, const char *s, int *status);

int  ext_command_exists(const char *name);

const char *suggest_command(const char *name);
char       *suggest_fix(struct yaitso_ctx *c, const char *line);

int   utf8_len(const char *s);
int   utf8_decode(const char *s, uint32_t *cp);
int   utf8_encode(uint32_t cp, char *out);
int   utf8_width(uint32_t cp);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void  xfree(void *p);
int   split_words(const char *s, char ***out, int *outn);
char *trim(char *s);
int   is_blank(const char *s);
int   parse_redirect(const char *s, char **clean, char **infile,
                     char **outfile, int *append, char **errfile,
                     int *errappend);
void  print_version(void);
void  print_help(void);

const char *env_get(struct yaitso_ctx *c, const char *k);
int         env_set(struct yaitso_ctx *c, const char *k, const char *v);
int         env_unset(struct yaitso_ctx *c, const char *k);
char      **env_array(struct yaitso_ctx *c);
void        env_free_array(char **arr);

const char *alias_get(struct yaitso_ctx *c, const char *name);
int         alias_set(struct yaitso_ctx *c, const char *name, const char *value);
int         alias_unset(struct yaitso_ctx *c, const char *name);

const char *posarg_get(struct yaitso_ctx *c, int idx);

#endif
