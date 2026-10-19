#ifndef TUI_THEME_H
#define TUI_THEME_H

// Colour themes for the terminal editor.
//
// Colours are 24-bit RGB; FTXUI downgrades them to the nearest entry of the
// terminal's palette when it does not support truecolor, so themes also work
// on 16- and 256-colour terminals.
//
// "Terminal default" reproduces the original look: syntax and markdown use the
// 16-colour palette and the chrome is drawn with `inverted`, i.e. the
// terminal's own foreground/background swap. Named themes set explicit
// foreground/background pairs instead, plus a screen background.

#include <cstdint>
#include <cstddef>

#include <ftxui/screen/color.hpp>

#include "tui/config.h"

namespace tui_theme {

using ftxui::Color;

inline Color rgb(uint32_t value) {
    return Color::RGB(static_cast<uint8_t>(value >> 16),
                      static_cast<uint8_t>(value >> 8),
                      static_cast<uint8_t>(value));
}

struct Theme {
    // Chrome uses the terminal's own colours rather than explicit pairs.
    bool inverted_chrome = true;

    Color fg;                                 // default text
    Color bg;                                 // screen background
    Color chrome_fg, chrome_bg;               // status bar, menu bar, windows
    Color highlight_fg, highlight_bg;         // selected rows, focused buttons
    Color selection_fg, selection_bg;         // selected document text
    Color cursor_fg, cursor_bg;               // text cursor
    Color accent;                             // picker directories, markers

    Color keyword, string, comment, number, preproc;  // syntax styles
    Color heading, code, strong, emphasis, quote,     // markdown styles
          list_marker, link, rule;
};

struct ThemeSpec {
    uint32_t fg, bg;
    uint32_t chrome_fg, chrome_bg;
    uint32_t highlight_fg, highlight_bg;
    uint32_t selection_fg, selection_bg;
    uint32_t cursor_fg, cursor_bg;
    uint32_t accent;
    uint32_t keyword, string, comment, number, preproc;
    uint32_t heading, code, strong, emphasis, quote, list_marker, link, rule;
};

// One row per named theme, in the order of AppSettings::ThemeId (excluding
// TerminalDefault), matching kThemeNames in config.cpp.
static const ThemeSpec kThemes[] = {
    // Dracula
    {0xF8F8F2, 0x282A36,
     0xF8F8F2, 0x44475A,
     0xF8F8F2, 0x6272A4,
     0xF8F8F2, 0x44475A,
     0x282A36, 0xF8F8F2,
     0x8BE9FD,
     0xFF79C6, 0xF1FA8C, 0x6272A4, 0xBD93F9, 0xFF79C6,
     0x8BE9FD, 0x50FA7B, 0xF1FA8C, 0xFFB86C, 0x6272A4, 0xFF79C6, 0x8BE9FD, 0x6272A4},
    // Nord
    {0xD8DEE9, 0x2E3440,
     0xECEFF4, 0x3B4252,
     0xECEFF4, 0x434C5E,
     0xECEFF4, 0x434C5E,
     0x2E3440, 0xD8DEE9,
     0x88C0D0,
     0x81A1C1, 0xA3BE8C, 0x616E88, 0xB48EAD, 0xD08770,
     0x88C0D0, 0xA3BE8C, 0xEBCB8B, 0xD08770, 0x616E88, 0xB48EAD, 0x88C0D0, 0x4C566A},
    // Gruvbox Dark
    {0xEBDBB2, 0x282828,
     0xEBDBB2, 0x3C3836,
     0xEBDBB2, 0x504945,
     0xEBDBB2, 0x504945,
     0x282828, 0xEBDBB2,
     0x8EC07C,
     0xFB4934, 0xB8BB26, 0x928374, 0xD3869B, 0xFE8019,
     0x8EC07C, 0xB8BB26, 0xFABD2F, 0xFE8019, 0x928374, 0xD3869B, 0x83A598, 0x928374},
    // Gruvbox Light
    {0x3C3836, 0xFBF1C7,
     0x3C3836, 0xEBDBB2,
     0x3C3836, 0xD5C4A1,
     0x3C3836, 0xD5C4A1,
     0xFBF1C7, 0x3C3836,
     0x427B58,
     0x9D0006, 0x79740E, 0x928374, 0x8F3F71, 0xAF3A03,
     0x427B58, 0x79740E, 0xB57614, 0xAF3A03, 0x928374, 0x8F3F71, 0x076678, 0x928374},
    // Solarized Dark
    {0x839496, 0x002B36,
     0x93A1A1, 0x073642,
     0xFDF6E3, 0x586E75,
     0x93A1A1, 0x073642,
     0x002B36, 0x93A1A1,
     0x268BD2,
     0x268BD2, 0x2AA198, 0x586E75, 0x6C71C4, 0xCB4B16,
     0x268BD2, 0x859900, 0xB58900, 0xCB4B16, 0x586E75, 0x6C71C4, 0x2AA198, 0x586E75},
    // Solarized Light
    {0x657B83, 0xFDF6E3,
     0x586E75, 0xEEE8D5,
     0xFDF6E3, 0x93A1A1,
     0x586E75, 0xEEE8D5,
     0xFDF6E3, 0x657B83,
     0x268BD2,
     0x268BD2, 0x2AA198, 0x93A1A1, 0x6C71C4, 0xCB4B16,
     0x268BD2, 0x859900, 0xB58900, 0xCB4B16, 0x93A1A1, 0x6C71C4, 0x2AA198, 0x93A1A1},
    // Tokyo Night
    {0xC0CAF5, 0x1A1B26,
     0xC0CAF5, 0x16161E,
     0xC0CAF5, 0x292E42,
     0xC0CAF5, 0x292E42,
     0x1A1B26, 0xC0CAF5,
     0x7AA2F7,
     0xBB9AF7, 0x9ECE6A, 0x565F89, 0xFF9E64, 0xF7768E,
     0x7DCFFF, 0x9ECE6A, 0xE0AF68, 0xFF9E64, 0x565F89, 0xBB9AF7, 0x7AA2F7, 0x565F89},
    // Catppuccin Mocha
    {0xCDD6F4, 0x1E1E2E,
     0xCDD6F4, 0x313244,
     0xCDD6F4, 0x45475A,
     0xCDD6F4, 0x45475A,
     0x1E1E2E, 0xCDD6F4,
     0x94E2D5,
     0xCBA6F7, 0xA6E3A1, 0x6C7086, 0xFAB387, 0xF38BA8,
     0x89B4FA, 0xA6E3A1, 0xF9E2AF, 0xFAB387, 0x6C7086, 0xCBA6F7, 0x89B4FA, 0x6C7086},
    // Catppuccin Latte
    {0x4C4F69, 0xEFF1F5,
     0x4C4F69, 0xCCD0DA,
     0x4C4F69, 0xBCC0CC,
     0x4C4F69, 0xBCC0CC,
     0xEFF1F5, 0x4C4F69,
     0x179299,
     0x8839EF, 0x40A02B, 0x9CA0B0, 0xFE640B, 0xD20F39,
     0x1E66F5, 0x40A02B, 0xDF8E1D, 0xFE640B, 0x9CA0B0, 0x8839EF, 0x1E66F5, 0x9CA0B0},
    // One Dark
    {0xABB2BF, 0x282C34,
     0xABB2BF, 0x21252B,
     0xABB2BF, 0x3E4451,
     0xABB2BF, 0x3E4451,
     0x282C34, 0xABB2BF,
     0x56B6C2,
     0xC678DD, 0x98C379, 0x5C6370, 0xD19A66, 0xE06C75,
     0x61AFEF, 0x98C379, 0xE5C07B, 0xD19A66, 0x5C6370, 0xC678DD, 0x61AFEF, 0x5C6370},
    // Kanagawa
    {0xDCD7BA, 0x1F1F28,
     0xDCD7BA, 0x2A2A37,
     0xDCD7BA, 0x223249,
     0xDCD7BA, 0x223249,
     0x1F1F28, 0xDCD7BA,
     0x7AA89F,
     0x957FB8, 0x98BB6C, 0x727169, 0xFFA066, 0xE82424,
     0x7FB4CA, 0x98BB6C, 0xE6C384, 0xFFA066, 0x727169, 0x957FB8, 0x7FB4CA, 0x727169},
};

constexpr std::size_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
static_assert(kThemeCount == static_cast<std::size_t>(AppSettings::ThemeId::Count) - 1,
              "kThemes must hold one palette per named AppSettings::ThemeId");

// Theme for an id; TerminalDefault keeps the terminal's own colours.
inline Theme make_theme(AppSettings::ThemeId id) {
    Theme theme;
    if (id == AppSettings::ThemeId::TerminalDefault) {
        theme.inverted_chrome = true;
        theme.fg = Color::White;
        theme.accent = Color::CyanLight;
        theme.keyword = Color::CyanLight;
        theme.string = Color::YellowLight;
        theme.comment = Color::GrayDark;
        theme.number = Color::RedLight;
        theme.preproc = Color::GreenLight;
        theme.heading = Color::CyanLight;
        theme.code = Color::GreenLight;
        theme.strong = Color::YellowLight;
        theme.emphasis = Color::Yellow;
        theme.quote = Color::GrayLight;
        theme.list_marker = Color::RedLight;
        theme.link = Color::BlueLight;
        theme.rule = Color::GrayDark;
        return theme;
    }

    const int index = static_cast<int>(id) - 1;
    if (index < 0 || index >= static_cast<int>(kThemeCount)) return theme;
    const ThemeSpec &s = kThemes[index];
    theme.inverted_chrome = false;
    theme.fg = rgb(s.fg);
    theme.bg = rgb(s.bg);
    theme.chrome_fg = rgb(s.chrome_fg);
    theme.chrome_bg = rgb(s.chrome_bg);
    theme.highlight_fg = rgb(s.highlight_fg);
    theme.highlight_bg = rgb(s.highlight_bg);
    theme.selection_fg = rgb(s.selection_fg);
    theme.selection_bg = rgb(s.selection_bg);
    theme.cursor_fg = rgb(s.cursor_fg);
    theme.cursor_bg = rgb(s.cursor_bg);
    theme.accent = rgb(s.accent);
    theme.keyword = rgb(s.keyword);
    theme.string = rgb(s.string);
    theme.comment = rgb(s.comment);
    theme.number = rgb(s.number);
    theme.preproc = rgb(s.preproc);
    theme.heading = rgb(s.heading);
    theme.code = rgb(s.code);
    theme.strong = rgb(s.strong);
    theme.emphasis = rgb(s.emphasis);
    theme.quote = rgb(s.quote);
    theme.list_marker = rgb(s.list_marker);
    theme.link = rgb(s.link);
    theme.rule = rgb(s.rule);
    return theme;
}

}  // namespace tui_theme

#endif  // TUI_THEME_H
