#include "tui/clipboard.h"

#include <array>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
// Resolve helpers from the Windows system directory only: a tampered PATH
// entry must not receive the decrypted clipboard text.
std::string system_command(const char *relative)
{
    char dir[MAX_PATH];
    const UINT n = GetSystemDirectoryA(dir, static_cast<UINT>(sizeof dir));
    if (n == 0 || n >= sizeof dir) return std::string();
    return std::string(dir) + "\\" + relative;
}

const char *kPasteRel = "WindowsPowerShell\\v1.0\\powershell.exe -NoProfile -Command Get-Clipboard";
#elif defined(__APPLE__)
// Fixed, canonical absolute paths (no $PATH lookup).
constexpr const char *kCopyCmd = "/usr/bin/pbcopy";
constexpr const char *kPasteCmd = "/usr/bin/pbpaste";
#else
// Linux: prefer Wayland, fall back to X11. Helpers are resolved from a
// fixed directory list rather than the caller's PATH, so a substituted
// wl-copy/xclip cannot receive the decrypted clipboard text.
std::string helper_path(const char *name)
{
    static const char *const kDirs[] = {"/usr/bin", "/usr/local/bin", "/bin"};
    for (const char *dir : kDirs) {
        std::string candidate = std::string(dir) + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return std::string();
}

std::string copy_cmd()
{
    static const std::string cmd = [] {
        std::string wl = helper_path("wl-copy");
        if (!wl.empty()) return wl;
        std::string xclip = helper_path("xclip");
        if (!xclip.empty()) return xclip + " -selection clipboard -in";
        return std::string();
    }();
    return cmd;
}

std::string paste_cmd()
{
    static const std::string cmd = [] {
        std::string wl = helper_path("wl-paste");
        if (!wl.empty()) return wl + " --no-newline";
        std::string xclip = helper_path("xclip");
        if (!xclip.empty()) return xclip + " -selection clipboard -out";
        return std::string();
    }();
    return cmd;
}
#endif

bool pipe_to(const std::string &cmd, const std::string &text) {
    if (cmd.empty()) return false;
    FILE *p = popen(cmd.c_str(), "w");
    if (!p) return false;
    const bool wrote = std::fwrite(text.data(), 1, text.size(), p) == text.size();
    return wrote && pclose(p) == 0;
}

bool pipe_from(const std::string &cmd, std::string &out) {
    if (cmd.empty()) return false;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return false;
    out.clear();
    std::array<char, 4096> buf{};
    std::size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0) out.append(buf.data(), n);
    const bool ok = pclose(p) == 0;
    buf.fill('\0');  // the read buffer holds clipboard text; wipe it
    return ok;
}

}  // namespace

bool clip::set_system(const std::string &text) {
#if defined(_WIN32)
    return pipe_to(system_command("clip.exe"), text);
#elif defined(__APPLE__)
    return pipe_to(kCopyCmd, text);
#else
    return pipe_to(copy_cmd(), text);
#endif
}

bool clip::get_system(std::string &out) {
#if defined(_WIN32)
    return pipe_from(system_command(kPasteRel), out);
#elif defined(__APPLE__)
    return pipe_from(kPasteCmd, out);
#else
    return pipe_from(paste_cmd(), out);
#endif
}
