#ifndef TUI_CLIPBOARD_H
#define TUI_CLIPBOARD_H

#include <string>

// Best-effort bridge to the host clipboard. An internal clipboard (kept by the
// app) is always authoritative; these helpers additionally push/pull from the
// OS clipboard when a supported mechanism is available.
//
// Order of attempts: macOS pbcopy/pbpaste, Windows clip/PowerShell,
// Linux wl-copy/wl-paste or xclip. Returns false if none worked.
namespace clip {
bool set_system(const std::string &text);
bool get_system(std::string &out);
}  // namespace clip

#endif  // TUI_CLIPBOARD_H
