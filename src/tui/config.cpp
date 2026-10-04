#include "tui/config.h"

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
}  // namespace

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
        << "newline_style=" << static_cast<int>(s.newline_style) << "\n";
}
