/* Crash reporting that cannot disclose protected material.
 * See crash_report.h for the contract. Everything that runs inside the
 * handler is async-signal-safe: no allocation, no stdio, no locale, no
 * snprintf. Output is formatted by hand into a static buffer.
 *
 * What is deliberately NOT recorded: register values, stack or heap bytes,
 * raw data addresses, argv, environment, file names, window titles. Code
 * addresses are reduced to "module+offset" so they are position independent.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "platform/crash_report.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <execinfo.h>
#include <link.h>
#define TXX_HAVE_BACKTRACE 1
#endif
#endif

#define REPORT_NAME   "crash-report.txt"
#define PREVIOUS_NAME "crash-report.previous.txt"
#define MAX_FRAMES    48
#define PATH_CAP      1024

static const char *g_app     = "encryptEd";
static const char *g_version = "?";
static char        g_path[PATH_CAP];      /* report file, UTF-8 (display / POSIX I/O) */
#ifndef _WIN32
static char        g_prev[PATH_CAP];      /* previous-report file (POSIX) */
#else
static wchar_t     g_wpath[PATH_CAP];
static wchar_t     g_wprev[PATH_CAP];
#endif

/* ------------------------------------------------------------------ */
/* Async-signal-safe text formatting                                  */
/* ------------------------------------------------------------------ */
static char   g_buf[4096];
static size_t g_len;

static void put(const char *s)
{
    while (*s && g_len < sizeof g_buf - 1) g_buf[g_len++] = *s++;
}

static void put_dec(unsigned long long v)
{
    char tmp[24];
    int  n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v != 0);
    while (n > 0 && g_len < sizeof g_buf - 1) g_buf[g_len++] = tmp[--n];
}

static void put_dec2(unsigned v)
{
    if (g_len + 2 >= sizeof g_buf) return;
    g_buf[g_len++] = (char)('0' + (v / 10) % 10);
    g_buf[g_len++] = (char)('0' + v % 10);
}

static void put_hex(unsigned long long v)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[20];
    int  n = 0;
    do { tmp[n++] = digits[v & 0xf]; v >>= 4; } while (v != 0);
    put("0x");
    while (n > 0 && g_len < sizeof g_buf - 1) g_buf[g_len++] = tmp[--n];
}

/* "YYYY-MM-DD HH:MM:SS UTC" from Unix seconds (Hinnant's civil_from_days). */
static void put_utc(long long t)
{
    long long days = t / 86400;
    long long rem  = t % 86400;
    if (rem < 0) { rem += 86400; --days; }
    long long z   = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y   = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp  = (5 * doy + 2) / 153;
    long long d   = doy - (153 * mp + 2) / 5 + 1;
    long long m   = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    put_dec((unsigned long long)y); put("-");
    put_dec2((unsigned)m);          put("-");
    put_dec2((unsigned)d);          put(" ");
    put_dec2((unsigned)(rem / 3600));        put(":");
    put_dec2((unsigned)(rem % 3600 / 60));   put(":");
    put_dec2((unsigned)(rem % 60));          put(" UTC");
}

static const char *platform_name(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unix";
#endif
}

static void put_header(void)
{
    g_len = 0;
    put("encryptEd crash report\n");
    put("application: "); put(g_app);      put("\n");
    put("version: ");     put(g_version);  put("\n");
    put("platform: ");    put(platform_name());
#if defined(__x86_64__) || defined(_M_X64)
    put(" x86_64");
#elif defined(__i386__) || defined(_M_IX86)
    put(" x86");
#elif defined(__aarch64__) || defined(_M_ARM64)
    put(" arm64");
#endif
    put("\ntime: ");
    put_utc((long long)time(NULL));
    put("\n");
}

static void put_footer(void)
{
    put("\nThis report holds no document text, passwords, keys, file names or\n"
        "memory contents. Resolve offsets with e.g. addr2line -e <binary> <offset>.\n");
}

/* ------------------------------------------------------------------ */
/* Module resolution: address -> "module" + offset                    */
/* ------------------------------------------------------------------ */
#if defined(TXX_HAVE_BACKTRACE) && defined(__linux__)
typedef struct { uintptr_t lo, hi, base; char name[48]; } module_t;
static module_t g_mods[64];
static int      g_nmods;

static int collect_module(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size; (void)data;
    if (g_nmods >= (int)(sizeof g_mods / sizeof g_mods[0])) return 1;
    module_t *m = &g_mods[g_nmods];
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        uintptr_t s = (uintptr_t)info->dlpi_addr + (uintptr_t)ph->p_vaddr;
        uintptr_t e = s + (uintptr_t)ph->p_memsz;
        if (s < lo) lo = s;
        if (e > hi) hi = e;
    }
    if (hi <= lo) return 0;
    m->lo = lo; m->hi = hi; m->base = (uintptr_t)info->dlpi_addr;
    const char *name = info->dlpi_name;
    if (name == NULL || *name == '\0') {
        name = g_app;                              /* the main executable */
    } else {
        const char *slash = strrchr(name, '/');    /* base name only */
        if (slash != NULL) name = slash + 1;
    }
    size_t n = strlen(name);
    if (n >= sizeof m->name) n = sizeof m->name - 1;
    memcpy(m->name, name, n);
    m->name[n] = '\0';
    ++g_nmods;
    return 0;
}

static void put_code_address(uintptr_t addr)
{
    for (int i = 0; i < g_nmods; ++i) {
        if (addr >= g_mods[i].lo && addr < g_mods[i].hi) {
            put(g_mods[i].name); put("+"); put_hex(addr - g_mods[i].base);
            return;
        }
    }
    put("?");
}
#endif

#ifdef _WIN32
static void put_code_address(uintptr_t addr)
{
    HMODULE mod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) && mod != NULL) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(mod, path, (DWORD)sizeof path);
        const char *name = path;
        if (n > 0 && n < sizeof path) {
            path[n] = '\0';
            for (const char *p = path; *p; ++p)
                if (*p == '\\' || *p == '/') name = p + 1;   /* base name only */
            put(name); put("+"); put_hex(addr - (uintptr_t)mod);
            return;
        }
    }
    put("?");
}
#endif

#if !defined(_WIN32) && !defined(TXX_HAVE_BACKTRACE)
static void put_code_address(uintptr_t addr) { (void)addr; put("?"); }
#endif

static void put_frames(void *const *frames, int n)
{
    put("stack trace (module+offset):\n");
    if (n <= 0) { put("  unavailable on this platform\n"); return; }
    for (int i = 0; i < n; ++i) {
        put("  #"); put_dec((unsigned long long)i); put(" ");
        put_code_address((uintptr_t)frames[i]);
        put("\n");
    }
}

/* ------------------------------------------------------------------ */
/* POSIX handler                                                      */
/* ------------------------------------------------------------------ */
#ifndef _WIN32

static void emit_report(void)
{
    int fd = g_path[0] != '\0'
                 ? open(g_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600)
                 : -1;
    for (int pass = 0; pass < 2; ++pass) {
        int out = pass == 0 ? fd : 2;
        size_t off = 0;
        while (out >= 0 && off < g_len) {
            ssize_t w = write(out, g_buf + off, g_len - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
    }
    if (fd >= 0) close(fd);
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGFPE:  return "SIGFPE (arithmetic error)";
    case SIGABRT: return "SIGABRT (abort: failed assertion, uncaught exception or detected heap corruption)";
    default:      return "signal";
    }
}

static void on_signal(int sig, siginfo_t *info, void *ctx)
{
    static volatile sig_atomic_t entered;
    (void)ctx;
    if (entered) _exit(128 + sig);
    entered = 1;

    void *frames[MAX_FRAMES];
    int   n = 0;
#ifdef TXX_HAVE_BACKTRACE
    n = backtrace(frames, MAX_FRAMES);
#endif

    put_header();
    put("reason: "); put(signal_name(sig));
    put(" ["); put_dec((unsigned long long)sig); put("]\n");
    if (info != NULL && sig != SIGABRT) {
        uintptr_t addr = (uintptr_t)info->si_addr;
        if (sig == SIGSEGV || sig == SIGBUS) {
            put("fault address: ");
            put(addr < 0x10000 ? "near null (null dereference?)" : "non-null (not recorded)");
            put("\n");
        }
    }
    put_frames(frames, n);
    put_footer();
    emit_report();

    signal(sig, SIG_DFL);
    raise(sig);          /* SA_NODEFER: delivered now, ends the process */
    _exit(128 + sig);
}

static void make_dirs(char *path)
{
    for (char *p = path + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        (void)mkdir(path, 0700);
        *p = '/';
    }
    (void)mkdir(path, 0700);
}

void txx_crash_report_install(const char *app_name, const char *version)
{
    if (app_name != NULL) g_app = app_name;
    if (version != NULL) g_version = version;

    const char *state = getenv("XDG_STATE_HOME");
    const char *home  = getenv("HOME");
    char dir[PATH_CAP];
    int  n;
    if (state != NULL && state[0] == '/')
        n = snprintf(dir, sizeof dir, "%s/%s", state, g_app);
    else if (home != NULL && home[0] == '/')
        n = snprintf(dir, sizeof dir, "%s/.local/state/%s", home, g_app);
    else
        n = -1;
    if (n > 0 && (size_t)n < sizeof dir - sizeof PREVIOUS_NAME - 2) {
        make_dirs(dir);
        snprintf(g_path, sizeof g_path, "%s/%s", dir, REPORT_NAME);
        snprintf(g_prev, sizeof g_prev, "%s/%s", dir, PREVIOUS_NAME);
    }

#ifdef TXX_HAVE_BACKTRACE
    /* Load the unwinder now: its first use may allocate. */
    void *warm[4];
    (void)backtrace(warm, 4);
#ifdef __linux__
    dl_iterate_phdr(collect_module, NULL);
#endif
#endif

    /* Alternate stack so a stack overflow can still be reported. */
    static char alt_stack[64 * 1024];
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof alt_stack;
    (void)sigaltstack(&ss, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER;
    const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; ++i)
        sigaction(sigs[i], &sa, NULL);
}

size_t txx_crash_report_take_previous(char *out, size_t cap)
{
    if (out == NULL || cap == 0) return 0;
    out[0] = '\0';
    if (g_path[0] == '\0') return 0;
    FILE *f = fopen(g_path, "rb");
    if (f == NULL) return 0;
    size_t got = fread(out, 1, cap - 1, f);
    fclose(f);
    out[got] = '\0';
    (void)rename(g_path, g_prev);
    return got;
}

#else /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Windows handler                                                    */
/* ------------------------------------------------------------------ */
static void emit_report(void)
{
    HANDLE file = g_wpath[0] != L'\0'
                      ? CreateFileW(g_wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, NULL)
                      : INVALID_HANDLE_VALUE;
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    for (int pass = 0; pass < 2; ++pass) {
        HANDLE h = pass == 0 ? file : err;
        if (h == NULL || h == INVALID_HANDLE_VALUE) continue;
        DWORD written = 0;
        (void)WriteFile(h, g_buf, (DWORD)g_len, &written, NULL);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

static const char *exception_name(DWORD code)
{
    switch (code) {
    case 0xC0000005: return "ACCESS_VIOLATION (invalid memory access)";
    case 0xC00000FD: return "STACK_OVERFLOW";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION";
    case 0xC0000094: return "INTEGER_DIVIDE_BY_ZERO";
    case 0xC0000374: return "HEAP_CORRUPTION";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN / fast fail";
    case 0xC0000096: return "PRIVILEGED_INSTRUCTION";
    case 0xC0000025: return "NONCONTINUABLE_EXCEPTION";
    case 0xE06D7363: return "C++ exception (uncaught)";
    default:         return "exception";
    }
}

static volatile LONG g_entered;

static LONG WINAPI on_exception(EXCEPTION_POINTERS *ep)
{
    if (InterlockedExchange(&g_entered, 1) != 0) TerminateProcess(GetCurrentProcess(), 3);

    void *frames[MAX_FRAMES];
    USHORT n = RtlCaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    put_header();
    put("reason: "); put(exception_name(code));
    put(" ["); put_hex(code); put("]\n");
    put("fault location: ");
    put_code_address((uintptr_t)ep->ExceptionRecord->ExceptionAddress);
    put("\n");
    if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
        put("access: ");
        put(kind == 0 ? "read" : kind == 1 ? "write" : "execute");
        put(addr < 0x10000 ? ", near null (null dereference?)" : ", non-null address (not recorded)");
        put("\n");
    }
    put_frames(frames, (int)n);
    put_footer();
    emit_report();

    TerminateProcess(GetCurrentProcess(), code);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void on_abort(int sig)
{
    (void)sig;
    if (InterlockedExchange(&g_entered, 1) != 0) TerminateProcess(GetCurrentProcess(), 3);

    void *frames[MAX_FRAMES];
    USHORT n = RtlCaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);

    put_header();
    put("reason: abort() (failed assertion, uncaught C++ exception or terminate)\n");
    put_frames(frames, (int)n);
    put_footer();
    emit_report();
    TerminateProcess(GetCurrentProcess(), 3);
}

void txx_crash_report_install(const char *app_name, const char *version)
{
    if (app_name != NULL) g_app = app_name;
    if (version != NULL) g_version = version;

    wchar_t wapp[128];
    if (MultiByteToWideChar(CP_UTF8, 0, g_app, -1, wapp, 128) > 0) {
        const wchar_t *base = _wgetenv(L"APPDATA");
        if (base != NULL && base[0] != L'\0' &&
            wcslen(base) + wcslen(wapp) + 40 < PATH_CAP) {
            wchar_t dir[PATH_CAP];
            swprintf(dir, PATH_CAP, L"%ls\\%ls", base, wapp);
            (void)CreateDirectoryW(dir, NULL);
            swprintf(g_wpath, PATH_CAP, L"%ls\\" L"" REPORT_NAME, dir);
            swprintf(g_wprev, PATH_CAP, L"%ls\\" L"" PREVIOUS_NAME, dir);
            if (WideCharToMultiByte(CP_UTF8, 0, g_wpath, -1, g_path, PATH_CAP, NULL, NULL) <= 0)
                g_path[0] = '\0';
        }
    }

    /* Keep enough stack to run the handler after a stack overflow. */
    ULONG guarantee = 64 * 1024;
    (void)SetThreadStackGuarantee(&guarantee);

    SetUnhandledExceptionFilter(on_exception);
    signal(SIGABRT, on_abort);
}

size_t txx_crash_report_take_previous(char *out, size_t cap)
{
    if (out == NULL || cap == 0) return 0;
    out[0] = '\0';
    if (g_wpath[0] == L'\0') return 0;
    FILE *f = _wfopen(g_wpath, L"rb");
    if (f == NULL) return 0;
    size_t got = fread(out, 1, cap - 1, f);
    fclose(f);
    out[got] = '\0';
    (void)MoveFileExW(g_wpath, g_wprev, MOVEFILE_REPLACE_EXISTING);
    return got;
}

#endif /* _WIN32 */

const char *txx_crash_report_path(void)
{
    return g_path;
}
