#define _GNU_SOURCE
#include "yat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <strings.h>
#include "ed25519.h"

#define REPO_DIR     "/repo"
#define INSTALLED    "/var/lib/yat/installed"
#define CACHE_DIR    "/var/cache/yat"
#define DB_DIR       "/var/lib/yat"
#define GLIBC_APPS   "/var/lib/yat/glibc-apps.list"
#define TRUSTED_KEYS "/etc/yat/trusted.keys"
#define REPOS_CONF   "/etc/yat/repos.conf"

struct pkg_inst {
    char name[128];
    char version[64];
    char libc[32];
    char files[256];
};

static char *g_repo_url = NULL;

static void mkdir_p(const char *p, mode_t m)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", p);
    size_t l = strlen(tmp);
    if (l && tmp[l-1] == '/') tmp[l-1] = 0;
    for (char *q = tmp + 1; *q; q++) {
        if (*q == '/') { *q = 0; mkdir(tmp, m); *q = '/'; }
    }
    mkdir(tmp, m);
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[r] = 0;
    if (out_len) *out_len = r;
    return buf;
}

static int write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(data, 1, n, f);
    fclose(f);
    return 0;
}

static void config_load(void)
{
    FILE *f = fopen(REPOS_CONF, "r");
    if (!f) { g_repo_url = strdup("https://repo.yaitso.local"); return; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p;
        char *v = eq + 1;
        while (*v == ' ') v++;
        char *e = v + strlen(v);
        while (e > v && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!strcmp(k, "local") || !strcmp(k, "default") || !strcmp(k, "url")) {
            free(g_repo_url);
            g_repo_url = strdup(v);
        }
    }
    fclose(f);
    if (!g_repo_url) g_repo_url = strdup("https://repo.yaitso.local");
}

static int check_signature(const char *index_path, const char *sig_path)
{
    size_t ilen = 0, slen = 0;
    char *idx = read_file(index_path, &ilen);
    char *sig = read_file(sig_path, &slen);
    if (!idx || !sig) { free(idx); free(sig); return -1; }
    if (slen != 64) { free(idx); free(sig); return -1; }

    char *keys = read_file(TRUSTED_KEYS, NULL);
    if (!keys) { free(idx); free(sig); return -1; }

    int ok = 0;
    char *line = strtok(keys, "\n");
    while (line) {
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '#') {
            if (ed25519_verify((const unsigned char *)idx, ilen,
                               (const unsigned char *)sig,
                               line) == 0) {
                ok = 1;
                break;
            }
        }
        line = strtok(NULL, "\n");
    }
    free(keys);
    free(idx);
    free(sig);
    return ok ? 0 : -1;
}

/* ---- безопасность: проверки имён, запуск без шелла, подпись и хеши ---- */

static int signing_required(void)
{
    const char *v = getenv("YAT_ALLOW_UNSIGNED");
    return !(v && !strcmp(v, "1"));
}

/* Имя пакета или версии: [A-Za-z0-9][A-Za-z0-9._+-]*, без "..", до maxlen. */
static int valid_token(const char *s, size_t maxlen)
{
    if (!s) return 0;
    size_t n = strlen(s);
    if (n == 0 || n > maxlen) return 0;
    if (!isalnum((unsigned char)s[0])) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '.' || c == '_' || c == '+' || c == '-')) return 0;
    }
    if (strstr(s, "..")) return 0;
    return 1;
}

/* Путь из .FILES: относительный, без "..", только безопасный набор символов. */
static int valid_relpath(const char *s)
{
    if (!s || !*s || s[0] == '/') return 0;
    if (strstr(s, "..")) return 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '/' || c == '@'))
            return 0;
    }
    return 1;
}

/* Запуск программы без шелла: аргументы не интерпретируются. */
static int run_argv(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Ищет в index.txt строку "<name>-<version>[.yat] <sha512-hex>". */
static int index_hash_for(const char *pkg_key, char out[129])
{
    char *idx = read_file(REPO_DIR "/index.txt", NULL);
    if (!idx) return -1;
    int found = -1;
    char *save = NULL;
    char *line = strtok_r(idx, "\n", &save);
    while (line) {
        char *s2 = NULL;
        char *name = strtok_r(line, " \t\r", &s2);
        char *hash = strtok_r(NULL, " \t\r", &s2);
        if (name) {
            size_t l = strlen(name);
            if (l > 4 && !strcmp(name + l - 4, ".yat")) name[l - 4] = 0;
            if (!strcmp(name, pkg_key) && hash && strlen(hash) == 128) {
                snprintf(out, 129, "%s", hash);
                found = 0;
                break;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(idx);
    return found;
}

/* Хеш пакета (sha512) должен совпасть с хешем из подписанного индекса. */
static int verify_package(const char *tarball, const char *pkg_key)
{
    char want[129];
    if (index_hash_for(pkg_key, want) != 0) {
        fprintf(stderr, "yat: в индексе нет хеша для %s\n", pkg_key);
        return -1;
    }
    size_t n = 0;
    char *data = read_file(tarball, &n);
    if (!data) return -1;
    unsigned char dig[64];
    yat_sha512((const unsigned char *)data, n, dig);
    free(data);
    char got[129];
    for (int i = 0; i < 64; i++) snprintf(got + i * 2, 3, "%02x", dig[i]);
    if (strcasecmp(got, want) != 0) {
        fprintf(stderr, "yat: хеш %s не совпадает с индексом\n", pkg_key);
        return -1;
    }
    return 0;
}

/* Индекс обязан быть подписан доверенным ключом (кроме явного YAT_ALLOW_UNSIGNED=1). */
static int require_trusted_index(void)
{
    if (!signing_required()) return 0;
    char idx_path[512], sig_path[512];
    snprintf(idx_path, sizeof(idx_path), "%s/index.txt", REPO_DIR);
    snprintf(sig_path, sizeof(sig_path), "%s/index.sig", REPO_DIR);
    if (check_signature(idx_path, sig_path) != 0) {
        fprintf(stderr, "yat: индекс не подписан доверенным ключом (%s); "
                        "для локального репо без подписи: YAT_ALLOW_UNSIGNED=1\n", TRUSTED_KEYS);
        return -1;
    }
    return 0;
}

static int extract_yat(const char *tarball, const char *dest)
{
    char *const argv[] = {"tar", "-xzf", (char *)tarball, "-C", (char *)dest, NULL};
    return run_argv(argv);
}

static int copy_tree(const char *src, const char *dst)
{
    char from[700], to[700];
    snprintf(from, sizeof(from), "%s/.", src);
    snprintf(to, sizeof(to), "%s/", dst);
    char *const argv[] = {"cp", "-a", from, to, NULL};
    return run_argv(argv);
}

static int pkg_install_one(const char *name, const char *version, int upgrade);

static int resolve_deps(const char *depends, int upgrade)
{
    if (!depends || !*depends) return 0;
    char *copy = strdup(depends);
    char *save = NULL;
    char *tok = strtok_r(copy, " \t", &save);
    while (tok) {
        if (!valid_token(tok, 127)) {
            fprintf(stderr, "yat: недопустимое имя зависимости: %s\n", tok);
            free(copy);
            return -1;
        }
        int need = 1;
        char inst_path[512];
        snprintf(inst_path, sizeof(inst_path), "%s/%s", INSTALLED, tok);
        if (access(inst_path, F_OK) == 0 && !upgrade) need = 0;
        if (need) {
            if (pkg_install_one(tok, NULL, upgrade) != 0) {
                fprintf(stderr, "yat: не удалось установить зависимость %s\n", tok);
                free(copy);
                return -1;
            }
        }
        tok = strtok_r(NULL, " \t", &save);
    }
    free(copy);
    return 0;
}

static int pkg_install_one(const char *name, const char *version, int upgrade)
{
    char tarball[1024];

    if (!valid_token(name, 127) || (version && !valid_token(version, 63))) {
        fprintf(stderr, "yat: недопустимое имя пакета: %s\n", name ? name : "(null)");
        return -1;
    }

    if (version) {
        snprintf(tarball, sizeof(tarball), "%s/%s-%s.yat", REPO_DIR, name, version);
    } else {
        DIR *d = opendir(REPO_DIR);
        if (!d) { fprintf(stderr, "yat: нет репо %s\n", REPO_DIR); return -1; }
        struct dirent *de;
        int found = 0;
        while ((de = readdir(d))) {
            size_t l = strlen(de->d_name);
            if (l < 5) continue;
            if (strncmp(de->d_name, name, strlen(name)) != 0) continue;
            if (de->d_name[strlen(name)] != '-') continue;
            if (strcmp(de->d_name + l - 4, ".yat")) continue;
            snprintf(tarball, sizeof(tarball), "%s/%s", REPO_DIR, de->d_name);
            found = 1;
            break;
        }
        closedir(d);
        if (!found) { fprintf(stderr, "yat: пакет %s не найден\n", name); return -1; }
    }

    if (access(tarball, R_OK) != 0) {
        fprintf(stderr, "yat: %s недоступен\n", tarball);
        return -1;
    }

    if (require_trusted_index() != 0) return -1;
    if (signing_required()) {
        char key[256];
        snprintf(key, sizeof(key), "%s", strrchr(tarball, '/') + 1);
        size_t kl = strlen(key);
        if (kl > 4 && !strcmp(key + kl - 4, ".yat")) key[kl - 4] = 0;
        if (verify_package(tarball, key) != 0) return -1;
    }

    char work[512];
    snprintf(work, sizeof(work), "%s/work-XXXXXX", CACHE_DIR);
    mkdir_p(CACHE_DIR, 0755);
    char *wd = mkdtemp(work);
    if (!wd) { perror("mkdtemp"); return -1; }

    char stage[600];
    /* stage - соседний каталог: копировать рабочий каталог внутрь самого себя нельзя. */
    snprintf(stage, sizeof(stage), "%s.stage", wd);
    mkdir_p(stage, 0755);

    if (extract_yat(tarball, wd) != 0) {
        fprintf(stderr, "yat: распаковка не удалась\n");
        return -1;
    }

    struct pkg_meta meta;
    if (yat_meta_read(wd, &meta) != 0) {
        fprintf(stderr, "yat: нет META в пакете\n");
        return -1;
    }
    if (!valid_token(meta.name, 127) || !valid_token(meta.version, 63)) {
        fprintf(stderr, "yat: недопустимые name/version в META\n");
        return -1;
    }

    if (!upgrade) {
        char inst_path[512];
        snprintf(inst_path, sizeof(inst_path), "%s/%s", INSTALLED, meta.name);
        if (access(inst_path, F_OK) == 0) {
            fprintf(stderr, "yat: %s уже установлен (используйте upgrade)\n",
                    meta.name);
            return -1;
        }
    }

    if (resolve_deps(meta.depends, upgrade) != 0) return -1;

    if (meta.libc[0] && !strcmp(meta.libc, "glibc")) {
        char glibc_marker[512];
        snprintf(glibc_marker, sizeof(glibc_marker), "%s/glibc", INSTALLED);
        if (access(glibc_marker, F_OK) != 0) {
            fprintf(stderr, "yat: требует glibc, устанавливаю...\n");
            if (pkg_install_one("glibc", NULL, 0) != 0) {
                fprintf(stderr, "yat: glibc не установлен\n");
                return -1;
            }
        }
    }

    printf(">>> установка %s %s\n", meta.name, meta.version);

    if (copy_tree(wd, stage) != 0) {
        fprintf(stderr, "yat: копирование не удалось\n");
        return -1;
    }

    char files_list[600];
    snprintf(files_list, sizeof(files_list), "%s/.FILES", wd);
    FILE *fl = fopen(files_list, "r");
    if (fl) {
        char line[1024];
        int bad = 0;
        while (fgets(line, sizeof(line), fl)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            if (!strcmp(line, "META") || !strcmp(line, ".FILES")) continue;
            if (!valid_relpath(line)) {
                fprintf(stderr, "yat: недопустимый путь в .FILES: %s\n", line);
                bad = 1;
                break;
            }
        }
        if (bad) { fclose(fl); return -1; }
        rewind(fl);
        while (fgets(line, sizeof(line), fl)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            if (!strcmp(line, "META") || !strcmp(line, ".FILES")) continue;
            char src[1700], dst[1100];
            snprintf(src, sizeof(src), "%s/%s", wd, line);
            snprintf(dst, sizeof(dst), "/%s", line);
            /* Не используем install(1): в образе под этим именем лежит установщик ОС. */
            char parent[1100];
            snprintf(parent, sizeof(parent), "%s", dst);
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent) {
                *slash = 0;
                mkdir_p(parent, 0755);
            }
            char *const acp2[] = {"cp", "-a", src, dst, NULL};
            run_argv(acp2);
        }
        fclose(fl);
    }

    if (meta.libc[0] && !strcmp(meta.libc, "glibc")) {
        char meta_ref[700];
        snprintf(meta_ref, sizeof(meta_ref), "%s/META", wd);
        char *const afind[] = {"find", "/opt", "/usr/bin", "-maxdepth", "2", "-type", "f",
                               "-newer", meta_ref, "-exec", "yglibc", "patch", "{}", ";", NULL};
        run_argv(afind);
    }

    char inst_dir[512];
    snprintf(inst_dir, sizeof(inst_dir), "%s/%s", INSTALLED, meta.name);
    mkdir_p(inst_dir, 0755);
    char meta_src[600], meta_dst[600];
    snprintf(meta_src, sizeof(meta_src), "%s/META", wd);
    snprintf(meta_dst, sizeof(meta_dst), "%s/META", inst_dir);
    char *const acp[] = {"cp", meta_src, meta_dst, NULL};
    run_argv(acp);
    /* .FILES сохраняем рядом с META: по нему yat remove узнаёт, что удалять. */
    char inst_files[600];
    snprintf(inst_files, sizeof(inst_files), "%s/.FILES", inst_dir);
    char *const acpf[] = {"cp", files_list, inst_files, NULL};
    run_argv(acpf);

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "/var/log/yat/history.log");
    mkdir_p("/var/log/yat", 0755);
    FILE *log = fopen(log_path, "a");
    if (log) {
        fprintf(log, "%ld %s %s %s\n", (long)time(NULL),
                upgrade ? "upgrade" : "install", meta.name, meta.version);
        fclose(log);
    }

    char *const arm[] = {"rm", "-rf", wd, NULL};
    run_argv(arm);
    char *const arm_stage[] = {"rm", "-rf", stage, NULL};
    run_argv(arm_stage);

    printf(">>> %s установлен\n", meta.name);
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "yat install <pkg>...\n"); return 1; }
    int rc = 0;
    for (int i = 0; i < argc; i++) {
        if (pkg_install_one(argv[i], NULL, 0) != 0) rc = 1;
    }
    return rc;
}

static int cmd_remove(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "yat remove <pkg>...\n"); return 1; }
    int rc = 0;
    for (int i = 0; i < argc; i++) {
        if (!valid_token(argv[i], 127)) {
            fprintf(stderr, "yat: недопустимое имя пакета: %s\n", argv[i]);
            rc = 1;
            continue;
        }
        char inst_dir[512];
        snprintf(inst_dir, sizeof(inst_dir), "%s/%s", INSTALLED, argv[i]);
        if (access(inst_dir, F_OK) != 0) {
            fprintf(stderr, "yat: %s не установлен\n", argv[i]);
            rc = 1;
            continue;
        }
        char files[600];
        snprintf(files, sizeof(files), "%s/.FILES", inst_dir);
        FILE *fl = fopen(files, "r");
        if (fl) {
            char line[1024];
            while (fgets(line, sizeof(line), fl)) {
                line[strcspn(line, "\r\n")] = 0;
                if (!line[0]) continue;
                if (!valid_relpath(line)) continue;
                char abs_path[1100];
                snprintf(abs_path, sizeof(abs_path), "/%s", line);
                unlink(abs_path);
            }
            fclose(fl);
        }
        char *const arm2[] = {"rm", "-rf", inst_dir, NULL};
        run_argv(arm2);
        printf(">>> %s удалён\n", argv[i]);
    }
    return rc;
}

/* Ищет самую новую версию пакета в репозитории. */
static int find_repo_version(const char *name, char *out, size_t n)
{
    DIR *d = opendir(REPO_DIR);
    if (!d) return 0;
    size_t nl = strlen(name);
    int found = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *f = de->d_name;
        if (strncmp(f, name, nl) != 0 || f[nl] != '-') continue;
        size_t l = strlen(f);
        if (l < nl + 6 || strcmp(f + l - 4, ".yat")) continue;
        char ver[64];
        size_t vl = l - nl - 1 - 4;
        if (vl == 0 || vl >= sizeof(ver)) continue;
        memcpy(ver, f + nl + 1, vl);
        ver[vl] = 0;
        if (!valid_token(ver, 63)) continue;
        if (!found || strverscmp(ver, out) > 0) {
            snprintf(out, n, "%s", ver);
            found = 1;
        }
    }
    closedir(d);
    return found;
}

static int cmd_upgrade(int argc, char **argv)
{
    (void)argc; (void)argv;
    DIR *d = opendir(INSTALLED);
    if (!d) { fprintf(stderr, "yat: нет установленных пакетов\n"); return 1; }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char inst_dir[512];
        snprintf(inst_dir, sizeof(inst_dir), "%s/%s", INSTALLED, de->d_name);
        struct pkg_meta m;
        if (yat_meta_read(inst_dir, &m) != 0) continue;

        char latest[64];
        if (!find_repo_version(de->d_name, latest, sizeof(latest))) continue;
        if (strverscmp(latest, m.version) <= 0) continue;

        printf(">>> обновление %s: %s -> %s\n", de->d_name, m.version, latest);
        if (pkg_install_one(de->d_name, latest, 1) != 0) rc = 1;
    }
    closedir(d);
    return rc;
}

static int cmd_list(int argc, char **argv)
{
    (void)argc; (void)argv;
    DIR *d = opendir(INSTALLED);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char meta_path[512];
        snprintf(meta_path, sizeof(meta_path), "%s/%s/META", INSTALLED, de->d_name);
        struct pkg_meta m;
        memset(&m, 0, sizeof(m));
        FILE *f = fopen(meta_path, "r");
        if (!f) continue;
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = 0;
            char *v = colon + 1;
            while (*v == ' ') v++;
            v[strcspn(v, "\r\n")] = 0;
            if (!strcmp(line, "version")) snprintf(m.version, sizeof(m.version), "%s", v);
            if (!strcmp(line, "libc"))    snprintf(m.libc, sizeof(m.libc), "%s", v);
        }
        fclose(f);
        printf("%-30s %-15s %s\n", de->d_name, m.version,
               m.libc[0] ? m.libc : "");
    }
    closedir(d);
    return 0;
}

static int cmd_search(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "yat search <query>\n"); return 1; }
    DIR *d = opendir(REPO_DIR);
    if (!d) return 1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strstr(de->d_name, argv[0]))
            printf("%s\n", de->d_name);
    }
    closedir(d);
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "yat info <pkg>\n"); return 1; }
    if (!valid_token(argv[0], 127)) { fprintf(stderr, "yat: недопустимое имя\n"); return 1; }
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/%s/META", INSTALLED, argv[0]);
    FILE *f = fopen(meta_path, "r");
    if (!f) {
        DIR *d = opendir(REPO_DIR);
        if (!d) return 1;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, argv[0], strlen(argv[0])) == 0) {
                printf("(repo) %s\n", de->d_name);
            }
        }
        closedir(d);
        return 0;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) fputs(line, stdout);
    fclose(f);
    return 0;
}

static int cmd_update(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!g_repo_url) config_load();
    printf(">>> обновление индекса с %s\n", g_repo_url);
    char url[1024];
    snprintf(url, sizeof(url), "%s/index.txt", g_repo_url);
    struct http_buf buf;
    if (http_get(url, &buf) != 0) {
        fprintf(stderr, "yat: не удалось скачать индекс\n");
        return 1;
    }
    char sig_url[1024];
    snprintf(sig_url, sizeof(sig_url), "%s/index.sig", g_repo_url);
    struct http_buf sig = {0};
    int have_sig = (http_get(sig_url, &sig) == 0);

    mkdir_p(REPO_DIR, 0755);
    char new_idx[512], new_sig[512], idx_path[512], sig_path[512];
    snprintf(new_idx, sizeof(new_idx), "%s/index.txt.new", REPO_DIR);
    snprintf(new_sig, sizeof(new_sig), "%s/index.sig.new", REPO_DIR);
    snprintf(idx_path, sizeof(idx_path), "%s/index.txt", REPO_DIR);
    snprintf(sig_path, sizeof(sig_path), "%s/index.sig", REPO_DIR);
    write_file(new_idx, buf.data, buf.len);
    if (have_sig) write_file(new_sig, sig.data, sig.len);
    free(buf.data);
    free(sig.data);

    /* Новый индекс принимаем только с валидной подписью (если подпись не отключена). */
    int ok = !signing_required() || (have_sig && check_signature(new_idx, new_sig) == 0);
    if (!ok) {
        unlink(new_idx);
        unlink(new_sig);
        fprintf(stderr, "yat: индекс отклонён: подпись не проходит проверку\n");
        return 1;
    }
    rename(new_idx, idx_path);
    if (have_sig) rename(new_sig, sig_path);
    else unlink(sig_path);
    printf(">>> индекс обновлён\n");
    return 0;
}

static int cmd_verify(int argc, char **argv)
{
    (void)argv;
    if (argc < 1) { fprintf(stderr, "yat verify <pkg>\n"); return 1; }
    char idx_path[512];
    snprintf(idx_path, sizeof(idx_path), "%s/index.txt", REPO_DIR);
    char sig_path[512];
    snprintf(sig_path, sizeof(sig_path), "%s/index.sig", REPO_DIR);
    if (check_signature(idx_path, sig_path) != 0) {
        fprintf(stderr, "yat: подпись невалидна\n");
        return 1;
    }
    printf("yat: подпись OK\n");
    return 0;
}

static int cmd_why(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "yat why <pkg>\n"); return 1; }
    DIR *d = opendir(INSTALLED);
    if (!d) return 1;
    struct dirent *de;
    printf("пакеты, зависящие от %s:\n", argv[0]);
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char meta_path[512];
        snprintf(meta_path, sizeof(meta_path), "%s/%s/META", INSTALLED, de->d_name);
        FILE *f = fopen(meta_path, "r");
        if (!f) continue;
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "depends:", 8) == 0 && strstr(line, argv[0]))
                printf("  %s\n", de->d_name);
        }
        fclose(f);
    }
    closedir(d);
    return 0;
}

static int cmd_graph(int argc, char **argv)
{
    (void)argc; (void)argv;
    DIR *d = opendir(INSTALLED);
    if (!d) return 1;
    struct dirent *de;
    printf("digraph deps {\n");
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char meta_path[512];
        snprintf(meta_path, sizeof(meta_path), "%s/%s/META", INSTALLED, de->d_name);
        FILE *f = fopen(meta_path, "r");
        if (!f) continue;
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "depends:", 8) == 0) {
                char *p = line + 8;
                while (*p == ' ') p++;
                char *save = NULL;
                char *tok = strtok_r(p, " \t\r\n", &save);
                while (tok) {
                    printf("  \"%s\" -> \"%s\";\n", de->d_name, tok);
                    tok = strtok_r(NULL, " \t\r\n", &save);
                }
            }
        }
        fclose(f);
    }
    closedir(d);
    printf("}\n");
    return 0;
}

static int cmd_pin(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "yat pin <pkg> <version>\n"); return 1; }
    if (!valid_token(argv[0], 127) || !valid_token(argv[1], 63)) {
        fprintf(stderr, "yat: недопустимое имя или версия\n");
        return 1;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/PIN", INSTALLED, argv[0]);
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "%s\n", argv[1]);
    fclose(f);
    printf(">>> %s pinned to %s\n", argv[0], argv[1]);
    return 0;
}

static int cmd_rollback(int argc, char **argv)
{
    (void)argc; (void)argv;
    FILE *f = fopen("/var/log/yat/history.log", "r");
    if (!f) { fprintf(stderr, "yat: нет истории\n"); return 1; }
    char *lines[256];
    int n = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), f) && n < 256)
        lines[n++] = strdup(buf);
    fclose(f);
    if (n == 0) return 0;
    printf("последние транзакции:\n");
    for (int i = n > 10 ? n - 10 : 0; i < n; i++)
        printf("  %s", lines[i]);
    for (int i = 0; i < n; i++) free(lines[i]);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "yat 0.3.2 — пакетный менеджер Yaitso OS\n"
        "usage: yat <command> [args]\n"
        "  install <pkg>...   — установить\n"
        "  remove <pkg>...    — удалить\n"
        "  upgrade            — обновить всё\n"
        "  list               — список установленных\n"
        "  search <query>     — поиск\n"
        "  info <pkg>         — информация\n"
        "  update             — обновить индекс\n"
        "  verify             — проверить подпись\n"
        "  why <pkg>          — кто зависит\n"
        "  graph              — граф зависимостей\n"
        "  pin <pkg> <ver>    — закрепить версию\n"
        "  rollback           — откат\n");
}

int main(int argc, char **argv)
{
    config_load();
    mkdir_p(DB_DIR, 0755);
    mkdir_p(INSTALLED, 0755);
    mkdir_p(CACHE_DIR, 0755);

    if (argc < 2) { usage(); return 2; }
    const char *cmd = argv[1];
    int a = argc - 2;
    char **v = argv + 2;

    if (!strcmp(cmd, "install"))  return cmd_install(a, v);
    if (!strcmp(cmd, "remove") || !strcmp(cmd, "uninstall"))
        return cmd_remove(a, v);
    if (!strcmp(cmd, "upgrade"))  return cmd_upgrade(a, v);
    if (!strcmp(cmd, "list"))     return cmd_list(a, v);
    if (!strcmp(cmd, "search"))   return cmd_search(a, v);
    if (!strcmp(cmd, "info"))     return cmd_info(a, v);
    if (!strcmp(cmd, "update"))   return cmd_update(a, v);
    if (!strcmp(cmd, "verify"))   return cmd_verify(a, v);
    if (!strcmp(cmd, "why"))      return cmd_why(a, v);
    if (!strcmp(cmd, "graph"))    return cmd_graph(a, v);
    if (!strcmp(cmd, "pin"))      return cmd_pin(a, v);
    if (!strcmp(cmd, "rollback")) return cmd_rollback(a, v);

    usage();
    return 2;
}
