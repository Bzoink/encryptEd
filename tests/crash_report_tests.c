#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
/* Crash reports must exist, be useful, and disclose nothing. Each case
 * crashes a forked child that holds a recognisable "secret" in its heap, its
 * environment and its argv, then inspects the report the handler wrote. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int main(void) { puts("crash_report_tests: POSIX only, skipped."); return 0; }
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/crash_report.h"

#define SECRET "hunter2-SECRET-PLAINTEXT"

static int failures;
#define CHECK(cond)                                                      \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static volatile char *g_secret_heap;

static int recurse(volatile int depth)
{
    volatile char pad[512];
    pad[0] = (char)depth;
    return recurse(depth + 1) + pad[0];
}

static void crash(int kind)
{
    switch (kind) {
    case 0: { volatile int *p = NULL; *p = 1; break; }
    case 1: abort();
    case 2: recurse(0); break;
    }
}

static void run_case(const char *state_dir, int kind, int expect_sig, const char *expect_text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/encryptEdTest/crash-report.txt", state_dir);
    remove(path);

    pid_t pid = fork();
    if (pid == 0) {
        g_secret_heap = malloc(64);
        strcpy((char *)g_secret_heap, SECRET);
        setenv("TXX_TEST_SECRET", SECRET, 1);
        txx_crash_report_install("encryptEdTest", "9.9");
        freopen("/dev/null", "w", stderr);
        crash(kind);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == expect_sig);

    char text[4096] = {0};
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f == NULL) return;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';

    CHECK(strstr(text, "encryptEd crash report") != NULL);
    CHECK(strstr(text, "version: 9.9") != NULL);
    CHECK(strstr(text, expect_text) != NULL);
    CHECK(strstr(text, "+0x") != NULL);          /* a module+offset frame */
    CHECK(strstr(text, SECRET) == NULL);
    CHECK(strstr(text, "hunter2") == NULL);
    CHECK(strchr(text, '/') == NULL);            /* no paths of any kind */
    CHECK(strstr(text, state_dir) == NULL);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 077) == 0); /* private file */
}

int main(void)
{
    char dir[] = "/tmp/txx-crash-XXXXXX";
    if (mkdtemp(dir) == NULL) { perror("mkdtemp"); return 1; }
    setenv("XDG_STATE_HOME", dir, 1);

    run_case(dir, 0, SIGSEGV, "SIGSEGV");
    run_case(dir, 0, SIGSEGV, "near null");
    run_case(dir, 1, SIGABRT, "SIGABRT");
    run_case(dir, 2, SIGSEGV, "SIGSEGV");        /* stack overflow, alt stack */

    /* The next start picks the report up exactly once. */
    txx_crash_report_install("encryptEdTest", "9.9");
    char out[2048];
    CHECK(txx_crash_report_take_previous(out, sizeof out) > 0);
    CHECK(strstr(out, "crash report") != NULL);
    CHECK(txx_crash_report_take_previous(out, sizeof out) == 0);

    if (failures == 0) puts("Crash report tests passed.");
    return failures == 0 ? 0 : 1;
}
#endif
