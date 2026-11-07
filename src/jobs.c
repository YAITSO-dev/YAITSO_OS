#define _GNU_SOURCE
#include "yaitso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>

struct job {
    int   id;
    pid_t pid;
    int   status;
    int   done;
    int   stopped;
    char  cmd[256];
};

static struct job g_jobs[YAITSO_MAX_JOBS];
static int        g_njobs = 0;
static int        g_next_job_id = 1;

void jobs_init(void)
{
    g_njobs = 0;
    g_next_job_id = 1;
    memset(g_jobs, 0, sizeof(g_jobs));
}

int jobs_add(struct yaitso_ctx *c, pid_t pid, const char *cmd)
{
    if (g_njobs >= YAITSO_MAX_JOBS) return -1;
    int slot = -1;
    for (int i = 0; i < g_njobs; i++)
        if (g_jobs[i].done) { slot = i; break; }
    if (slot < 0) slot = g_njobs++;
    g_jobs[slot].id = g_next_job_id++;
    g_jobs[slot].pid = pid;
    g_jobs[slot].status = 0;
    g_jobs[slot].done = 0;
    g_jobs[slot].stopped = 0;
    snprintf(g_jobs[slot].cmd, sizeof(g_jobs[slot].cmd), "%s", cmd ? cmd : "");
    if (c) c->last_bg_pid = pid;
    return g_jobs[slot].id;
}

int jobs_count(void) { return g_njobs; }

void jobs_reap(struct yaitso_ctx *c)
{
    int st;
    pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG | WUNTRACED)) > 0) {
        for (int i = 0; i < g_njobs; i++) {
            if (g_jobs[i].pid == p && !g_jobs[i].done) {
                if (WIFSTOPPED(st)) {
                    g_jobs[i].stopped = 1;
                    fprintf(stderr, "\n[%d]+ Stopped %s\n",
                            g_jobs[i].id, g_jobs[i].cmd);
                } else {
                    g_jobs[i].done = 1;
                    g_jobs[i].status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
                }
                if (c) c->last_status = g_jobs[i].status;
                break;
            }
        }
    }
}

void jobs_print(struct yaitso_ctx *c)
{
    (void)c;
    for (int i = 0; i < g_njobs; i++) {
        if (g_jobs[i].pid == 0) continue;
        const char *mark = "+";
        if (g_jobs[i].done) {
            printf("[%d]%s  Done      %s\n", g_jobs[i].id, mark, g_jobs[i].cmd);
        } else if (g_jobs[i].stopped) {
            printf("[%d]%s  Stopped   %s\n", g_jobs[i].id, mark, g_jobs[i].cmd);
        } else {
            printf("[%d]%s  Running   %s\n", g_jobs[i].id, mark, g_jobs[i].cmd);
        }
    }
}

int jobs_fg(struct yaitso_ctx *c, int id)
{
    int idx = -1;
    if (id <= 0) {
        for (int i = g_njobs - 1; i >= 0; i--)
            if (g_jobs[i].pid && !g_jobs[i].done) { idx = i; break; }
    } else {
        for (int i = 0; i < g_njobs; i++)
            if (g_jobs[i].id == id) { idx = i; break; }
    }
    if (idx < 0) { fprintf(stderr, "fg: нет такой задачи\n"); return 1; }

    pid_t p = g_jobs[idx].pid;
    printf("%s\n", g_jobs[idx].cmd);
    tcsetpgrp(STDIN_FILENO, p);
    kill(-p, SIGCONT);
    int st;
    waitpid(p, &st, WUNTRACED);
    tcsetpgrp(STDIN_FILENO, getpid());
    g_jobs[idx].done = 1;
    g_jobs[idx].status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    if (c) c->last_status = g_jobs[idx].status;
    return g_jobs[idx].status;
}

int jobs_bg(struct yaitso_ctx *c, int id)
{
    (void)c;
    int idx = -1;
    if (id <= 0) {
        for (int i = g_njobs - 1; i >= 0; i--)
            if (g_jobs[i].pid && !g_jobs[i].done) { idx = i; break; }
    } else {
        for (int i = 0; i < g_njobs; i++)
            if (g_jobs[i].id == id) { idx = i; break; }
    }
    if (idx < 0) { fprintf(stderr, "bg: нет такой задачи\n"); return 1; }
    kill(-g_jobs[idx].pid, SIGCONT);
    printf("[%d]+ %s &\n", g_jobs[idx].id, g_jobs[idx].cmd);
    return 0;
}

int jobs_wait_all(struct yaitso_ctx *c)
{
    int last = 0;
    for (int i = 0; i < g_njobs; i++) {
        if (g_jobs[i].pid && !g_jobs[i].done) {
            int st;
            waitpid(g_jobs[i].pid, &st, 0);
            g_jobs[i].done = 1;
            g_jobs[i].status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
            last = g_jobs[i].status;
        }
    }
    if (c) c->last_status = last;
    return last;
}
