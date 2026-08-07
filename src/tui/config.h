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
    enum StatusCount { STATUS_COUNT_CHARS = 0, STATUS_COUNT_BYTES = 1 };

    bool syn_highlight = true;
    IndentMode indent_mode = INDENT_TABS;
    int indent_width = 4;
    StatusCount status_count = STATUS_COUNT_CHARS;
    txx_newline_style newline_style = TXX_NEWLINE_LF;

    bool use_spaces() const { return indent_mode == INDENT_SPACES; }
};

// Path to the config file (creating parent directories is the caller's job on
// save). Returns empty string if no suitable location is found.
std::string config_path();
void config_load(AppSettings &s);
void config_save(const AppSettings &s);

#endif  // TUI_CONFIG_H
