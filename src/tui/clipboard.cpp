#include "tui/clipboard.h"

#include <array>
#include <cstdio>

namespace {
#if defined(_WIN32)
constexpr const char *kCopyCmd = "clip";
constexpr const char *kPasteCmd = "powershell -NoProfile -Command Get-Clipboard";
#elif defined(__APPLE__)
constexpr const char *kCopyCmd = "pbcopy";
constexpr const char *kPasteCmd = "pbpaste";
#else
// Linux: prefer Wayland, fall back to X11. The shell picks whichever exists.
constexpr const char *kCopyCmd =
    "command -v wl-copy >/dev/null 2>&1 && wl-copy || xclip -selection clipboard -in";
constexpr const char *kPasteCmd =
    "command -v wl-paste >/dev/null 2>&1 && wl-paste --no-newline || "
    "xclip -selection clipboard -out";
#endif

bool pipe_to(const char *cmd, const std::string &text) {
    FILE *p = popen(cmd, "w");
    if (!p) return false;
    std::fwrite(text.data(), 1, text.size(), p);
    return pclose(p) == 0;
}

bool pipe_from(const char *cmd, std::string &out) {
    FILE *p = popen(cmd, "r");
    if (!p) return false;
    out.clear();
    std::array<char, 4096> buf{};
    std::size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0) out.append(buf.data(), n);
    return pclose(p) == 0;
}
}  // namespace

bool clip::set_system(const std::string &text) { return pipe_to(kCopyCmd, text); }
bool clip::get_system(std::string &out) { return pipe_from(kPasteCmd, out); }
