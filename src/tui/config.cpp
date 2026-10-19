#include "tui/config.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
fs::path config_dir() {
#if defined(_WIN32)
    if (const char *appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "encryptEd";
#else
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"))
        return fs::path(xdg) / "encryptEd";
    if (const char *home = std::getenv("HOME"))
        return fs::path(home) / ".config" / "encryptEd";
#endif
    return fs::path();
}

// Must stay in the order of AppSettings::ThemeId and match the palettes in
// theme.h.
const char *const kThemeNames[] = {
    "terminal",
    "dracula",
    "nord",
    "gruvbox-dark",
    "gruvbox-light",
    "solarized-dark",
    "solarized-light",
    "tokyo-night",
    "catppuccin-mocha",
    "catppuccin-latte",
    "one-dark",
    "kanagawa",
};
constexpr int kThemeCount = static_cast<int>(sizeof(kThemeNames) / sizeof(kThemeNames[0]));
static_assert(kThemeCount == static_cast<int>(AppSettings::ThemeId::Count),
              "kThemeNames must list every AppSettings::ThemeId");
}  // namespace

const char *theme_name(AppSettings::ThemeId id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kThemeCount) return kThemeNames[0];
    return kThemeNames[index];
}

std::string theme_label(AppSettings::ThemeId id) {
    std::string label = theme_name(id);
    bool upper = true;
    for (char &c : label) {
        if (c == '-') { c = ' '; upper = true; continue; }
        if (upper) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); upper = false; }
    }
    return label;
}

AppSettings::ThemeId theme_from_name(const std::string &name) {
    for (int i = 0; i < kThemeCount; ++i)
        if (name == kThemeNames[i]) return static_cast<AppSettings::ThemeId>(i);
    return AppSettings::ThemeId::TerminalDefault;
}

std::string config_path() {
    fs::path dir = config_dir();
    if (dir.empty()) return std::string();
    return (dir / "config").string();
}

void config_load(AppSettings &s) {
    std::string path = config_path();
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "theme") { s.theme = theme_from_name(val); continue; }
        int n = 0;
        try { n = std::stoi(val); } catch (...) { continue; }
        if (key == "syn_highlight" || key == "markdown_highlight") s.syn_highlight = n != 0;
        else if (key == "vertical_scrollbar") s.vertical_scrollbar = n != 0;
        else if (key == "indent_mode") s.indent_mode = n ? AppSettings::INDENT_SPACES : AppSettings::INDENT_TABS;
        else if (key == "indent_width") s.indent_width = (n < 2 ? 2 : (n > 8 ? 8 : n));
        else if (key == "status_count") s.status_count = n >= 0 && n <= 2 ? static_cast<AppSettings::StatusCount>(n) : AppSettings::STATUS_COUNT_CHARS;
        else if (key == "newline_style") s.newline_style = static_cast<txx_newline_style>(n);
    }
}

void config_save(const AppSettings &s) {
    std::string path = config_path();
    if (path.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "syn_highlight=" << (s.syn_highlight ? 1 : 0) << "\n"
        << "vertical_scrollbar=" << (s.vertical_scrollbar ? 1 : 0) << "\n"
        << "indent_mode=" << static_cast<int>(s.indent_mode) << "\n"
        << "indent_width=" << s.indent_width << "\n"
        << "status_count=" << static_cast<int>(s.status_count) << "\n"
        << "newline_style=" << static_cast<int>(s.newline_style) << "\n"
        << "theme=" << theme_name(s.theme) << "\n";
}
