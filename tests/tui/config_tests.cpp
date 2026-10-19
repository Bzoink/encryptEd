// Unit tests for TUI preferences: theme names and config persistence.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "tui/config.h"

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void test_theme_names() {
    CHECK(std::string(theme_name(AppSettings::ThemeId::TerminalDefault)) == "terminal");
    CHECK(std::string(theme_name(AppSettings::ThemeId::Dracula)) == "dracula");
    CHECK(std::string(theme_name(AppSettings::ThemeId::CatppuccinMocha)) == "catppuccin-mocha");
    CHECK(theme_label(AppSettings::ThemeId::TerminalDefault) == "Terminal");
    CHECK(theme_label(AppSettings::ThemeId::GruvboxDark) == "Gruvbox Dark");
    CHECK(theme_label(AppSettings::ThemeId::OneDark) == "One Dark");

    const int count = static_cast<int>(AppSettings::ThemeId::Count);
    for (int i = 0; i < count; ++i) {
        const auto id = static_cast<AppSettings::ThemeId>(i);
        CHECK(theme_from_name(theme_name(id)) == id);  // every id round-trips
        for (int j = i + 1; j < count; ++j)
            CHECK(std::string(theme_name(id)) != std::string(theme_name(static_cast<AppSettings::ThemeId>(j))));
    }

    CHECK(theme_from_name("dracula") == AppSettings::ThemeId::Dracula);
    CHECK(theme_from_name("nonsense") == AppSettings::ThemeId::TerminalDefault);
    CHECK(theme_from_name("") == AppSettings::ThemeId::TerminalDefault);
    CHECK(theme_from_name("Dracula") == AppSettings::ThemeId::TerminalDefault);  // case-sensitive
}

static std::string temporary_config_dir() {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "encryptEd-config-tests";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

static void test_config_roundtrip() {
    const std::string dir = temporary_config_dir();
#if defined(_WIN32)
    _putenv_s("APPDATA", dir.c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
#endif
    CHECK(!config_path().empty());

    AppSettings saved;
    saved.theme = AppSettings::ThemeId::TokyoNight;
    saved.indent_width = 6;
    saved.vertical_scrollbar = false;
    config_save(saved);

    AppSettings loaded;
    config_load(loaded);
    CHECK(loaded.theme == AppSettings::ThemeId::TokyoNight);
    CHECK(loaded.indent_width == 6);
    CHECK(!loaded.vertical_scrollbar);
    CHECK(loaded.syn_highlight);  // untouched fields keep their defaults

    // An unknown theme value falls back to the terminal default.
    {
        std::ofstream out(config_path(), std::ios::trunc);
        out << "theme=bogus\nindent_width=3\n";
    }
    AppSettings fallback;
    config_load(fallback);
    CHECK(fallback.theme == AppSettings::ThemeId::TerminalDefault);
    CHECK(fallback.indent_width == 3);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

int main() {
    test_theme_names();
    test_config_roundtrip();

    if (g_failures == 0) {
        std::printf("all config tests passed\n");
        return 0;
    }
    std::printf("%d config test(s) failed\n", g_failures);
    return 1;
}
