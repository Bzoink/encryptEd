#ifndef TXX_CRASH_REPORT_H
#define TXX_CRASH_REPORT_H

#include <stddef.h>

/* Minimal crash reporting that cannot disclose protected material.
 *
 * On a fatal signal (POSIX) or unhandled exception / abort (Windows) the
 * handler writes one small text file and terminates the process at once.
 * The report contains ONLY:
 *   - application name and version, platform, UTC time
 *   - the signal / exception name and number
 *   - whether the faulting data address was near null (no raw address)
 *   - a stack trace as "module+0xoffset" code offsets (module base names
 *     only, never full paths)
 * It never contains memory contents, registers, stack data, argv, the
 * environment, window titles, document names or paths, passwords, keys or
 * any other user data. No dump file is produced and the process is not kept
 * alive (no dialog), so decrypted text does not linger after a crash.
 *
 * The handler itself is async-signal-safe: it allocates nothing, formats by
 * hand into a static buffer and uses only write()/WriteFile().
 *
 * Limits: a Windows heap-corruption fail-fast (HeapEnableTerminationOnCorruption)
 * and __fastfail() terminate the process inside the OS before any user-mode
 * handler can run; those leave no report. On glibc, heap corruption detected
 * by malloc ends in abort() and is reported as SIGABRT.
 */

/* Install handlers. `app_name` and `version` must be string literals or
 * otherwise live for the whole process. Call once, early in main().
 * Creates the report directory ($XDG_STATE_HOME/<app> or ~/.local/state/<app>
 * on Unix, %APPDATA%\<app> on Windows). Failure is silent: with no usable
 * directory the handler still prints the summary to stderr. */
void txx_crash_report_install(const char *app_name, const char *version);

/* If an earlier run left a report, copy up to cap-1 bytes into `out`
 * (NUL-terminated), move the file aside to "crash-report.previous.txt" so it
 * is shown only once, and return the number of bytes copied. Returns 0 when
 * there is none. Not for use from the crash handler. */
size_t txx_crash_report_take_previous(char *out, size_t cap);

/* Full path of the report file (UTF-8), or "" when unavailable. */
const char *txx_crash_report_path(void);

#endif
