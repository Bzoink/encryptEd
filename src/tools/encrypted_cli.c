#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

#include "core/txx_codec.h"
#include "core/txx_format.h"
#include "core/txx_status.h"
#include "platform/file_io.h"
#include "platform/secure_mem.h"

#define PASSWORD_MAX 4096U

static void wipe_cstr(char *text, size_t capacity)
{
    if (text != NULL && capacity != 0U) {
        txx_secure_zero(text, capacity);
    }
}

static int strip_newline(char *text)
{
    size_t len = strlen(text);
    while (len != 0U && (text[len - 1U] == '\n' || text[len - 1U] == '\r')) {
        text[--len] = '\0';
    }
    return 0;
}

static int read_password_line(char *password, size_t capacity)
{
    size_t len;

    if (capacity == 0U || fgets(password, (int)capacity, stdin) == NULL) {
        return -1;
    }
    len = strlen(password);
    if (len + 1U == capacity && len != 0U && password[len - 1U] != '\n') {
        int ch;
        do {
            ch = getchar();
        } while (ch != '\n' && ch != EOF);
        return -1;
    }
    return strip_newline(password);
}

static int read_password(char *password, size_t capacity, int from_stdin)
{
    int ok;

    if (from_stdin) {
        return read_password_line(password, capacity);
    }

#if defined(_WIN32)
    {
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD old_mode = 0;
        int restore = input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &old_mode);
        if (restore) {
            SetConsoleMode(input, old_mode & ~ENABLE_ECHO_INPUT);
        }
        fprintf(stderr, "Password: ");
        fflush(stderr);
        ok = read_password_line(password, capacity);
        fprintf(stderr, "\n");
        if (restore) {
            SetConsoleMode(input, old_mode);
        }
    }
#else
    {
        int fd = STDIN_FILENO;
        struct termios old_term;
        struct termios new_term;
        int restore = isatty(fd) && tcgetattr(fd, &old_term) == 0;
        if (restore) {
            new_term = old_term;
            new_term.c_lflag &= (tcflag_t)~ECHO;
            tcsetattr(fd, TCSAFLUSH, &new_term);
        }
        fprintf(stderr, "Password: ");
        fflush(stderr);
        ok = read_password_line(password, capacity);
        fprintf(stderr, "\n");
        if (restore) {
            tcsetattr(fd, TCSAFLUSH, &old_term);
        }
    }
#endif
    return ok;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s pack <input.txt> <output.txx> [--password-stdin]\n", program);
    fprintf(stderr, "  %s unpack <input.txx> <output.txt> [--password-stdin]\n", program);
}

int main(int argc, char **argv)
{
    txx_buffer input;
    txx_buffer output;
    char password[PASSWORD_MAX];
    txx_status status;
    int from_stdin = 0;
    int is_pack;
    int is_unpack;

    memset(password, 0, sizeof(password));
#if !defined(_WIN32)
    /* Decrypted documents and the password must not leak through crash
     * dumps; disable them outright (best effort). */
    {
        struct rlimit cores;
        cores.rlim_cur = 0;
        cores.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &cores);
    }
#endif
    if (argc == 5 && strcmp(argv[4], "--password-stdin") == 0) {
        from_stdin = 1;
    } else if (argc != 4) {
        usage(argv[0]);
        return 2;
    }

    is_pack = strcmp(argv[1], "pack") == 0;
    is_unpack = strcmp(argv[1], "unpack") == 0;
    if (!is_pack && !is_unpack) {
        usage(argv[0]);
        return 2;
    }

    status = txx_read_file_limited(argv[2], TXX_MAX_FILE_BYTES, &input);
    if (status != TXX_OK) {
        fprintf(stderr, "read failed: %s\n", txx_status_string(status));
        return 1;
    }

    if (read_password(password, sizeof(password), from_stdin) != 0) {
        fprintf(stderr, "password read failed\n");
        wipe_cstr(password, sizeof(password));
        txx_buffer_secure_free(&input);
        return 1;
    }

    txx_buffer_init(&output);
    if (is_pack) {
        status = txx_encode_text(input.data, input.len, password, NULL, &output);
    } else {
        status = txx_decode_text(input.data, input.len, password, &output);
    }
    wipe_cstr(password, sizeof(password));

    if (status == TXX_OK) {
        status = txx_write_file_atomic(argv[3], output.data, output.len);
    }

    txx_buffer_secure_free(&input);
    txx_buffer_secure_free(&output);
    if (status != TXX_OK) {
        fprintf(stderr, "operation failed: %s\n", txx_status_string(status));
        return 1;
    }

    return 0;
}