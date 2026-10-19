#ifndef TUI_CONFIG_H
#define TUI_CONFIG_H

#include <string>

extern "C" {
#include "core/utf8.h"
}

// Persistent preferences, mirroring the GUI's AppSettings minus the graphical
// font face/size. Stored as a tiny key=value file under the platform config dir
// ($XDG_CONFIG_HOME/encryptEd/config or %APPDATA%\encryptEd\config).
struct AppSettings {
    enum IndentMode { INDENT_TABS = 0, INDENT_SPACES = 1 };
    enum StatusCount { STATUS_COUNT_CHARS = 0, STATUS_COUNT_BYTES = 1, STATUS_COUNT_SIZE = 2 };

    // Colour themes. The order matches the name table in config.cpp and the
    // palettes in theme.h.
    enum class ThemeId : int {
        TerminalDefault = 0,
        Dracula,
        Nord,
        GruvboxDark,
        GruvboxLight,
        SolarizedDark,
        SolarizedLight,
        TokyoNight,
        CatppuccinMocha,
        CatppuccinLatte,
        OneDark,
        Kanagawa,
        Count,
    };

    bool syn_highlight = true;
    bool vertical_scrollbar = true;
    IndentMode indent_mode = INDENT_TABS;
    int indent_width = 4;
    StatusCount status_count = STATUS_COUNT_CHARS;
    txx_newline_style newline_style = TXX_NEWLINE_LF;
    ThemeId theme = ThemeId::TerminalDefault;

    bool use_spaces() const { return indent_mode == INDENT_SPACES; }
};

// Config-file name of a theme (e.g. "gruvbox-dark"); also its identity in the
// settings file. Unknown ids fall back to "terminal".
const char *theme_name(AppSettings::ThemeId id);

// Human-readable name for menus (e.g. "Gruvbox Dark").
std::string theme_label(AppSettings::ThemeId id);

// Theme for a config value; unrecognised names give ThemeId::TerminalDefault.
AppSettings::ThemeId theme_from_name(const std::string &name);

// Path to the config file (creating parent directories is the caller's job on
// save). Returns empty string if no suitable location is found.
std::string config_path();
void config_load(AppSettings &s);
void config_save(const AppSettings &s);

#endif  // TUI_CONFIG_H
