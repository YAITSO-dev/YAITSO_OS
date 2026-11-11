#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define GLIBC_PREFIX "/usr/glibc"
#define GLIBC_LD     GLIBC_PREFIX "/lib/ld-linux-x86-64.so.2"
#define GLIBC_LIB    GLIBC_PREFIX "/lib"

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static int is_elf(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char m[4];
    size_t r = fread(m, 1, 4, f);
    fclose(f);
    return r == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}

static int cmd_run(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "yglibc run: нужна команда\n");
        return 2;
    }

    if (!file_exists(GLIBC_LD)) {
        fprintf(stderr, "yglibc run: %s не найден\n", GLIBC_LD);
        return 1;
    }

    char **new_argv = calloc((size_t)argc + 4, sizeof(char *));
    if (!new_argv) return 1;
    new_argv[0] = (char *)GLIBC_LD;
    new_argv[1] = "--library-path";
    new_argv[2] = (char *)GLIBC_LIB;
    for (int i = 0; i < argc; i++)
        new_argv[i + 3] = argv[i];
    new_argv[argc + 3] = NULL;

    setenv("GCONV_PATH", GLIBC_PREFIX "/lib/gconv", 1);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); free(new_argv); return 1; }
    if (pid == 0) {
        execv(GLIBC_LD, new_argv);
        fprintf(stderr, "yglibc run: execv: %s\n", strerror(errno));
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    free(new_argv);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}

static int cmd_patch(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "yglibc patch: нужен файл\n");
        return 2;
    }
    const char *bin = argv[0];
    if (!file_exists(bin) || !is_elf(bin)) {
        fprintf(stderr, "yglibc patch: %s не ELF\n", bin);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        execlp("patchelf", "patchelf",
               "--set-interpreter", GLIBC_LD,
               "--set-rpath", GLIBC_LIB,
               (char *)bin, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        fprintf(stderr, "yglibc patch: patchelf не сработал\n");
        return 1;
    }
    return 0;
}

/* Ищет подстроку в файле целиком, а не только в первых 4 КиБ. */
static int file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t nl = strlen(needle);
    char buf[8192 + 64];
    size_t keep = 0;
    int found = 0;
    for (;;) {
        size_t r = fread(buf + keep, 1, sizeof(buf) - keep, f);
        size_t total = keep + r;
        if (total >= nl && memmem(buf, total, needle, nl)) { found = 1; break; }
        if (r == 0) break;
        keep = total < nl - 1 ? total : nl - 1;
        memmove(buf, buf + total - keep, keep);
    }
    fclose(f);
    return found;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "yglibc verify: нужен файл\n");
        return 2;
    }
    const char *bin = argv[0];
    if (!file_exists(bin)) { printf("%s: нет файла\n", bin); return 1; }
    if (!is_elf(bin))      { printf("%s: не ELF\n", bin);  return 1; }

    int has_glibc = file_contains(bin, GLIBC_LD) ||
                    file_contains(bin, "libc.so.6");
    printf("%s: %s\n", bin, has_glibc ? "glibc-compatible" : "other libc");
    return 0;
}

static int cmd_info(void)
{
    printf("glibc prefix: %s\n", GLIBC_PREFIX);
    printf("ld.so:        %s (%s)\n", GLIBC_LD,
           file_exists(GLIBC_LD) ? "ok" : "отсутствует");
    printf("libc:         %s/libc.so.6 (%s)\n", GLIBC_LIB,
           file_exists(GLIBC_LIB "/libc.so.6") ? "ok" : "отсутствует");

    pid_t pid = fork();
    if (pid == 0) {
        execl(GLIBC_LD, GLIBC_LD, "--version", (char *)NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  yglibc run <cmd> [args...]\n"
        "  yglibc patch <binary>\n"
        "  yglibc verify <binary>\n"
        "  yglibc info\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 2; }
    const char *sub = argv[1];
    if (!strcmp(sub, "run"))    return cmd_run(argc - 2, argv + 2);
    if (!strcmp(sub, "patch"))  return cmd_patch(argc - 2, argv + 2);
    if (!strcmp(sub, "verify")) return cmd_verify(argc - 2, argv + 2);
    if (!strcmp(sub, "info"))   return cmd_info();
    usage();
    return 2;
}
