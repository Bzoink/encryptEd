// etui — text-mode (terminal) front-end for encryptEd.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <ftxui/screen/string.hpp>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

extern "C" {
#include "core/txx_codec.h"
#include "core/txx_status.h"
#include "core/utf8.h"
#include "platform/file_io.h"
}

#include "app/about.h"
#include "app/size_format.h"
#include "gui/text_transform.h"
#include "tui/clipboard.h"
#include "tui/config.h"
#include "tui/plugin_buffer.h"
#include "tui/plugin_dialog.h"
#include "tui/edit_buffer.h"
#include "tui/md_parse.h"
#include "tui/syn_parse.h"
#include "tui/theme.h"

using namespace ftxui;
using namespace tui_theme;

namespace {

constexpr uint64_t kMaxFileBytes = 64ull * 1024 * 1024;

std::string newline_terminator(txx_newline_style s) {
    switch (s) {
        case TXX_NEWLINE_CRLF: return "\r\n";
        case TXX_NEWLINE_CR:   return "\r";
        default:               return "\n";
    }
}

const char *newline_label(txx_newline_style s) {
    switch (s) {
        case TXX_NEWLINE_CRLF:  return "CRLF";
        case TXX_NEWLINE_CR:    return "CR";
        case TXX_NEWLINE_MIXED: return "Mixed";
        default:                return "LF";
    }
}

std::string join_with_style(const std::string &lf_text, txx_newline_style style) {
    if (style == TXX_NEWLINE_LF || style == TXX_NEWLINE_MIXED) return lf_text;
    std::string term = newline_terminator(style);
    std::string out;
    out.reserve(lf_text.size() + lf_text.size() / 16);
    for (char c : lf_text) {
        if (c == '\n') out += term;
        else out.push_back(c);
    }
    return out;
}

}  // namespace

class EditorApp : public ComponentBase {
public:
    explicit EditorApp(App *screen, std::string open_path, std::string piped_text = "")
        : screen_(screen), pending_open_(std::move(open_path)) {
        config_load(settings_);
        theme_ = make_theme(settings_.theme);
        buffer_.set_text("");
        build_menus();
        set_status("New document");
        if (!piped_text.empty()) {
            // Document read from standard input: no path, so saving uses Save As.
            int mixed = 0;
            txx_newline_style nl = txx_text_detect_newlines(
                reinterpret_cast<const uint8_t *>(piped_text.data()), piped_text.size(), &mixed);
            if (nl != TXX_NEWLINE_UNKNOWN) settings_.newline_style = nl;
            buffer_.set_text(piped_text);
            filepath_.clear();
            password_.clear();
            dirty_ = true;
            syn_lang_ = SynLang::None;
            set_status("Read " + std::to_string(piped_text.size()) + " bytes from stdin (unsaved)");
        } else if (!pending_open_.empty()) {
            open_path_flow(pending_open_);
        }
    }

    bool Focusable() const override { return true; }

    Element OnRender() override {
        Dimensions dim = Terminal::Size();
        int rows = std::max(1, dim.dimy - 2);
        ensure_visible(rows);
        const int cols = text_cols();
        ensure_visible_h(cols);

        Elements doc;
        int total = buffer_.line_count();

        // Pre-scan lines above the viewport to establish inter-line state
        // (block comments, Python triple-quotes) in O(N) total per frame.
        SynState syn_st{};
        if (syn_lang_ != SynLang::None) {
            for (int l = 0; l < scroll_top_ && l < total; ++l)
                syn_parse_line(buffer_.line(l), syn_st, syn_lang_);
        }

        for (int i = 0; i < rows; ++i) {
            int line = scroll_top_ + i;
            doc.push_back(line < total ? render_line(line, syn_st, cols) : text(""));
        }

        Element editor = vbox(std::move(doc)) | flex;
        if (settings_.vertical_scrollbar && total > rows) {
            const int thumb_height = std::max(1, rows * rows / total);
            const int thumb_top = scroll_top_ * (rows - thumb_height) / (total - rows);
            Elements track;
            for (int i = 0; i < rows; ++i)
                track.push_back(text(i >= thumb_top && i < thumb_top + thumb_height ? "█" : "░"));
            editor = hbox({editor, vbox(std::move(track)) | size(WIDTH, EQUAL, 1)});
        }
        Element top    = render_menu_bar();
        Element bottom = render_status();
        Element base   = vbox({top, editor, bottom});
        if (!theme_.inverted_chrome) base = base | color(theme_.fg) | bgcolor(theme_.bg);

        if (menu_open_ >= 0)
            base = dbox({base, render_menu_overlay()});
        if (picker_open_)
            base = dbox({base, render_picker_overlay()});
        if (dialog_kind_ != DialogKind::None)
            base = dbox({base, render_dialog()});
        if (search_panel_open_)
            base = dbox({base, search_panel_->Render()});

        return base;
    }

    bool OnEvent(Event event) override {
        if (plugin_running_) return true;
        if (search_panel_open_) {
            if (event == Event::Escape) { search_panel_open_ = false; return true; }
            if (event == Event::F3 || event.input() == "\x1b[13~") {
                find_match(last_find_, false, buffer_.has_selection() && buffer_.selected_text() == last_find_);
                return true;
            }
            if (event == Event::F2 || event.input() == "\x1b[12~") {
                find_match(last_find_, true, true);
                return true;
            }
            search_panel_->OnEvent(event);
            return true;
        }
        if (menu_open_ >= 0)                  return menu_event(event);
        if (dialog_kind_ != DialogKind::None) return dialog_event(event);
        if (picker_open_)                     return picker_event(event);
        return editor_event(event);
    }

private:
    // ---- Menu data types ---------------------------------------------------
    struct MenuEntry {
        std::string label;       // "" = separator line
        std::string shortcut;
        std::function<void()> action;
        std::vector<MenuEntry> sub;
        std::function<bool()> checked;  // nullptr = not a toggle item
        bool checkbox = false;          // true = checkbox glyph, false = radio glyph
    };

    struct TopMenu {
        std::string title;
        std::vector<MenuEntry> items;
        mutable Box title_box{};
    };

    std::vector<TopMenu> menus_;
    int  menu_open_       = -1;   // open top-level index; -1 = closed
    int  menu_item_       = -1;   // highlighted item in dropdown
    int  menu_sub_        = -1;   // highlighted item in submenu
    bool menu_in_sub_     = false;
    bool menu_mouse_held_ = false;

    // ---- Menu construction -------------------------------------------------
    void build_menus() {
        // ---- File ----------------------------------------------------------
        std::vector<MenuEntry> fi;
        fi.push_back({"New",       "Ctrl+N", [this]{ cmd_new(); }});
        fi.push_back({"Open...",   "Ctrl+O", [this]{ cmd_open(); }});
        fi.push_back({});
        fi.push_back({"Save",      "Ctrl+S", [this]{ cmd_save(false); }});
        fi.push_back({"Save As...", "",      [this]{ cmd_save(true); }});
        fi.push_back({});
        // Line Endings submenu
        std::vector<MenuEntry> le;
        le.push_back({"LF (Unix, macOS)",  "", [this]{ set_newline(TXX_NEWLINE_LF);   }, {},
            [this]{ return settings_.newline_style == TXX_NEWLINE_LF;   }});
        le.push_back({"CRLF (Windows)",    "", [this]{ set_newline(TXX_NEWLINE_CRLF); }, {},
            [this]{ return settings_.newline_style == TXX_NEWLINE_CRLF; }});
        le.push_back({"CR (Classic Mac)",  "", [this]{ set_newline(TXX_NEWLINE_CR);   }, {},
            [this]{ return settings_.newline_style == TXX_NEWLINE_CR;   }});
        le.push_back({"Mixed (Preserve)",  "", [this]{ set_newline(TXX_NEWLINE_MIXED);}, {},
            [this]{ return settings_.newline_style == TXX_NEWLINE_MIXED;}});
        fi.push_back({"Line Endings", "", nullptr, std::move(le)});
        fi.push_back({});
        fi.push_back({"Quit", "Ctrl+Q", [this]{ cmd_quit(); }});
        menus_.push_back({"File", std::move(fi)});

        // ---- Edit ----------------------------------------------------------
        std::vector<MenuEntry> ed;
        ed.push_back({"Undo",       "Alt+Z", [this]{ edit([&]{ buffer_.undo(); }); }});
        ed.push_back({"Redo",       "Ctrl+Y", [this]{ edit([&]{ buffer_.redo(); }); }});
        ed.push_back({});
        ed.push_back({"Cut",        "Ctrl+X", [this]{ cmd_cut(); }});
        ed.push_back({"Copy",       "Ctrl+C", [this]{ cmd_copy(); }});
        ed.push_back({"Paste",      "Ctrl+V", [this]{ cmd_paste(); }});
        ed.push_back({"Select All", "Ctrl+A", [this]{ buffer_.select_all(); }});
        ed.push_back({});
        ed.push_back({"Find...",       "Ctrl+F", [this]{ cmd_find(); }});
        ed.push_back({"Find Next", "F3", [this]{ repeat_find(false); }});
        ed.push_back({"Find Previous", "F2", [this]{ repeat_find(true); }});
        ed.push_back({"Find / Replace...", "Ctrl+R", [this]{ cmd_replace(); }});
        ed.push_back({"Go to Line...", "Ctrl+G", [this]{ cmd_goto(); }});
        // Text transforms submenu
        std::vector<MenuEntry> tx;
        tx.push_back({"Convert Tabs to Spaces", "", [this]{
            transform([&](const std::string &t){ return tt_tabs_to_spaces(t, settings_.indent_width); }); }});
        tx.push_back({"Convert Spaces to Tabs", "", [this]{
            transform([&](const std::string &t){ return tt_spaces_to_tabs(t, settings_.indent_width); }); }});
        tx.push_back({"Trim Trailing Spaces",   "", [this]{ transform(tt_trim_trailing); }});
        tx.push_back({"Remove Blank Lines",     "", [this]{ transform(tt_remove_blank_lines); }});
        tx.push_back({"Column Wrap...",         "", [this]{ cmd_column_wrap(); }});
        tx.push_back({"Ensure Trailing Newline","", [this]{ transform(tt_ensure_trailing_newline); }});
        tx.push_back({});
        tx.push_back({"Duplicate Line", "Ctrl+D", [this]{ edit([&]{ buffer_.duplicate_line(); }); }});
        tx.push_back({"Remove Line",    "Ctrl+K", [this]{ edit([&]{ buffer_.remove_line(); }); }});
        ed.push_back({"Text", "", nullptr, std::move(tx)});
        menus_.push_back({"Edit", std::move(ed)});
        plugin_catalog_ = plugins::discover(plugin_root_);
        menus_.push_back({"Plugins", plugin_menu_entries()});

        // ---- Settings ------------------------------------------------------
        std::vector<MenuEntry> se;
        se.push_back({"Vertical Scrollbar", "", [this]{
            settings_.vertical_scrollbar = !settings_.vertical_scrollbar;
            config_save(settings_);
        }, {}, [this]{ return settings_.vertical_scrollbar; }, true});
        se.push_back({"Syntax Highlighting", "", [this]{
            settings_.syn_highlight = !settings_.syn_highlight;
            config_save(settings_);
        }, {}, [this]{ return settings_.syn_highlight; }, true});
        se.push_back({});
        se.push_back({"Indent: Spaces", "", [this]{
            settings_.indent_mode = AppSettings::INDENT_SPACES; config_save(settings_);
        }, {}, [this]{ return settings_.use_spaces(); }});
        se.push_back({"Indent: Tabs", "", [this]{
            settings_.indent_mode = AppSettings::INDENT_TABS; config_save(settings_);
        }, {}, [this]{ return !settings_.use_spaces(); }});
        se.push_back({});
        se.push_back({"Status: Char Count", "", [this]{
            settings_.status_count = AppSettings::STATUS_COUNT_CHARS; config_save(settings_);
        }, {}, [this]{ return settings_.status_count == AppSettings::STATUS_COUNT_CHARS; }});
        se.push_back({"Status: Byte Count", "", [this]{
            settings_.status_count = AppSettings::STATUS_COUNT_BYTES; config_save(settings_);
        }, {}, [this]{ return settings_.status_count == AppSettings::STATUS_COUNT_BYTES; }});
        se.push_back({"Status: Readable Size", "", [this]{
            settings_.status_count = AppSettings::STATUS_COUNT_SIZE; config_save(settings_);
        }, {}, [this]{ return settings_.status_count == AppSettings::STATUS_COUNT_SIZE; }});
        se.push_back({});
        // ---- Theme submenu -------------------------------------------------
        std::vector<MenuEntry> themes;
        for (int i = 0; i < static_cast<int>(AppSettings::ThemeId::Count); ++i) {
            const auto id = static_cast<AppSettings::ThemeId>(i);
            themes.push_back({theme_label(id), "", [this, id]{
                settings_.theme = id;
                theme_ = make_theme(id);
                config_save(settings_);
            }, {}, [this, id]{ return settings_.theme == id; }});
        }
        se.push_back({"Theme", "", nullptr, std::move(themes)});
        menus_.push_back({"Settings", std::move(se)});

        // ---- Help ----------------------------------------------------------
        std::vector<MenuEntry> he;
        he.push_back({"About", "", [this]{ cmd_about(); }});
        menus_.push_back({"Help", std::move(he)});
    }

    // ---- Rendering ---------------------------------------------------------
    Color style_color(MdStyle s) const {
        switch (s) {
            case MdStyle::Heading:    return theme_.heading;
            case MdStyle::CodeBlock:
            case MdStyle::InlineCode: return theme_.code;
            case MdStyle::Strong:     return theme_.strong;
            case MdStyle::Emphasis:   return theme_.emphasis;
            case MdStyle::Quote:      return theme_.quote;
            case MdStyle::ListMarker: return theme_.list_marker;
            case MdStyle::Link:       return theme_.link;
            case MdStyle::Rule:       return theme_.rule;
            default:                  return theme_.fg;
        }
    }

    Color syn_color(SynStyle s) const {
        switch (s) {
            case SynStyle::Keyword: return theme_.keyword;
            case SynStyle::String:  return theme_.string;
            case SynStyle::Comment: return theme_.comment;
            case SynStyle::Number:  return theme_.number;
            case SynStyle::Preproc: return theme_.preproc;
            default:                return theme_.fg;
        }
    }

    // Chrome styles: named themes paint explicit foreground/background pairs,
    // the terminal-default theme keeps the terminal's own inverted colours.
    Element chrome_style(Element e) const {
        if (theme_.inverted_chrome) return e | inverted;
        return e | color(theme_.chrome_fg) | bgcolor(theme_.chrome_bg);
    }

    Element highlight_style(Element e) const {
        if (theme_.inverted_chrome) return e | inverted;
        return e | color(theme_.highlight_fg) | bgcolor(theme_.highlight_bg);
    }

    Element selection_style(Element e) const {
        if (theme_.inverted_chrome) return e | inverted;
        return e | color(theme_.selection_fg) | bgcolor(theme_.selection_bg);
    }

    Element cursor_style(Element e) const {
        if (theme_.inverted_chrome) return e | inverted;
        return e | color(theme_.cursor_fg) | bgcolor(theme_.cursor_bg);
    }

    Element render_menu_bar() {
        Elements parts;
        for (int i = 0; i < (int)menus_.size(); ++i) {
            const std::string &title = menus_[i].title;
            // First letter is underlined as the Alt+letter shortcut hint.
            // Active title gets double-inverted (stands out as a "hole").
            Element t = hbox({
                text(" "),
                text(title.substr(0, 1)) | underlined,
                text(title.substr(1) + " "),
            });
            if (i == menu_open_) t = highlight_style(t);
            t = t | reflect(menus_[i].title_box);
            parts.push_back(t);
        }
        parts.push_back(filler());
        return chrome_style(hbox(std::move(parts)));
    }

    // Minimum inner content width for a list of items.
    static int items_inner_width(const std::vector<MenuEntry> &items) {
        int w = 16;
        for (const auto &e : items) {
            if (e.label.empty()) continue;
            int iw = 4 + (int)e.label.size();  // "  " prefix + label + " "
            if (!e.shortcut.empty()) iw += 2 + (int)e.shortcut.size();
            if (!e.sub.empty())      iw += 3;  // " → "
            w = std::max(w, iw);
        }
        return w;
    }

    // Build the rows for a dropdown list; `sel` is the highlighted index (-1 = none).
    Elements build_rows(const std::vector<MenuEntry> &items, int sel, int inner_w) {
        Elements rows;
        for (int i = 0; i < (int)items.size(); ++i) {
            const auto &e = items[i];
            if (e.label.empty()) { rows.push_back(separator()); continue; }

            Element row;
            if (e.checked) {
                auto state = EntryState{e.label, e.checked(), false, false, -1};
                auto base  = e.checkbox
                    ? CheckboxOption::Simple().transform(state)
                    : RadioboxOption::Simple().transform(state);
                if (!e.sub.empty()) {
                    row = hbox({std::move(base), filler(),
                                text(" → ")}) | size(WIDTH, EQUAL, inner_w);
                } else if (!e.shortcut.empty()) {
                    row = hbox({std::move(base), filler(),
                                text(e.shortcut + " ")}) | size(WIDTH, EQUAL, inner_w);
                } else {
                    row = hbox({std::move(base), filler()}) | size(WIDTH, EQUAL, inner_w);
                }
            } else {
                if (!e.sub.empty()) {
                    row = hbox({text("  " + e.label + " "), filler(),
                                text(" → ")}) | size(WIDTH, EQUAL, inner_w);
                } else if (!e.shortcut.empty()) {
                    row = hbox({text("  " + e.label + " "), filler(),
                                text(e.shortcut + " ")}) | size(WIDTH, EQUAL, inner_w);
                } else {
                    row = hbox({text("  " + e.label), filler()}) | size(WIDTH, EQUAL, inner_w);
                }
            }
            if (i == sel) row = highlight_style(row);
            rows.push_back(row);
        }
        return rows;
    }

    // Position element `e` at screen column `x`, screen row `y`, within a dbox layer.
    static Element place_at(int x, int y, Element e) {
        return vbox({
            text("") | size(HEIGHT, EQUAL, y),
            hbox({text("") | size(WIDTH, EQUAL, x), std::move(e), filler()}),
        });
    }

    Element render_menu_overlay() {
        if (menu_open_ < 0) return text("");
        const auto &menu  = menus_[menu_open_];
        const auto &items = menu.items;
        int iw  = items_inner_width(items);
        int ddw = iw + 2;  // +2 for left/right border columns
        int ddx = menu.title_box.x_min;

        int main_sel = menu_item_;
        auto rows = build_rows(items, main_sel, iw);
        Element dd = chrome_style(window(text(""), vbox(std::move(rows)))) | clear_under;

        Element overlay = place_at(ddx, 1, dd);

        // Submenu: open when an item with children is highlighted
        bool show_sub = menu_item_ >= 0 && menu_item_ < (int)items.size() &&
                        !items[menu_item_].sub.empty();
        if (show_sub) {
            const auto &sub_items = items[menu_item_].sub;
            int siw  = items_inner_width(sub_items);
            int sub_sel = menu_in_sub_ ? menu_sub_ : -1;
            auto sub_rows = build_rows(sub_items, sub_sel, siw);
            Element sub_dd = chrome_style(window(text(""), vbox(std::move(sub_rows)))) | clear_under;
            // x: right edge of main dropdown; y: aligned with triggering item row
            int sub_x = ddx + ddw;
            int sub_y = 1 + menu_item_;   // 1 = menu bar; menu_item_ = rows from top border
            overlay = dbox({overlay, place_at(sub_x, sub_y, sub_dd)});
        }

        return overlay;
    }

    Element render_line(int line_idx, SynState &syn_st, int cols) {
        const std::string &ln = buffer_.line(line_idx);
        const std::size_t v0 = slice_from_cell(ln, scroll_left_);
        const std::size_t v1 = slice_from_cell(ln, scroll_left_ + cols);
        Position sel_a = buffer_.selection_start();
        Position sel_b = buffer_.selection_end();
        bool has_sel     = buffer_.has_selection();
        bool line_in_sel = has_sel && line_idx >= sel_a.line && line_idx <= sel_b.line;

        if (line_in_sel) {
            // Selection rendering: update syn state but skip highlighting.
            if (syn_lang_ != SynLang::None)
                syn_parse_line(ln, syn_st, syn_lang_);
            int from = (line_idx == sel_a.line) ? sel_a.col : 0;
            int to   = (line_idx == sel_b.line) ? sel_b.col : buffer_.line_length(line_idx);
            return render_selection_line(ln, from, to, cols);
        }

        // Locate cursor within this line so we can split a styled segment at it.
        bool show_cursor = (line_idx == buffer_.cursor().line &&
                            menu_open_ < 0 && dialog_kind_ == DialogKind::None &&
                            !picker_open_ && !search_panel_open_);
        std::size_t cur_b = 0, cur_clen = 0;
        if (show_cursor) {
            cur_b = byte_at(ln, buffer_.cursor().col);
            if (cur_b < ln.size()) {
                int clen = 0;
                utf8_decode_next(ln.c_str() + cur_b, ln.c_str() + ln.size(), &clen);
                cur_clen = clen > 0 ? static_cast<std::size_t>(clen) : 1;
            }
            // cur_clen == 0: cursor is past end of line → trailing space added below
        }

        // Flatten run list into (start, end, has_color, Color) segments so both
        // the syn and markdown paths share one cursor-insertion loop below.
        struct Seg { std::size_t a, b; bool has_col; Color col; };
        std::vector<Seg> segs;

        auto add_segs = [&](const auto &runs, auto cfn) {
            std::size_t pos = 0;
            for (const auto &r : runs) {
                if (r.start > pos)
                    segs.push_back({pos, r.start, false, {}});
                segs.push_back({r.start, r.start + r.len, true, cfn(r.style)});
                pos = r.start + r.len;
            }
            if (pos < ln.size()) segs.push_back({pos, ln.size(), false, {}});
        };

        if (settings_.syn_highlight) {
            if (syn_lang_ != SynLang::None) {
                auto runs = syn_parse_line(ln, syn_st, syn_lang_);
                add_segs(runs, [this](SynStyle s){ return syn_color(s); });
            } else {
                auto runs = md_parse_line(ln, code_state(line_idx));
                add_segs(runs, [this](MdStyle s){ return style_color(s); });
            }
        } else {
            if (!ln.empty()) segs.push_back({0, ln.size(), false, {}});
        }

        // Convert to Elements, splitting the segment that contains the cursor.
        Elements out;
        for (const auto &seg : segs) {
            auto emit = [&](std::size_t a, std::size_t b, bool is_cur) {
                a = std::max(a, v0);
                b = std::min(b, v1);
                if (a >= b) return;
                Element e = text(ln.substr(a, b - a));
                if (seg.has_col) e = e | color(seg.col);
                if (is_cur) e = cursor_style(e) | focusCursorBarBlinking;
                out.push_back(std::move(e));
            };
            if (!show_cursor || cur_b < seg.a || cur_b >= seg.b) {
                emit(seg.a, seg.b, false);
            } else {
                emit(seg.a, cur_b, false);
                emit(cur_b, cur_b + cur_clen, true);
                emit(cur_b + cur_clen, seg.b, false);
            }
        }
        // Cursor past end of line: trailing inverted space (when in view).
        if (show_cursor && cur_clen == 0 && cell_at(ln, ln.size()) < scroll_left_ + cols)
            out.push_back(cursor_style(text(" ")) | focusCursorBarBlinking);

        // A double-width glyph straddling the left edge leaves a one-cell gap.
        const int pad = cell_at(ln, v0) - scroll_left_;
        if (pad > 0) out.insert(out.begin(), text(std::string(static_cast<std::size_t>(pad), ' ')));
        if (out.empty()) out.push_back(text(""));
        return hbox(std::move(out));
    }

    Element render_selection_line(const std::string &ln, int from, int to, int cols) {
        const std::size_t v0 = slice_from_cell(ln, scroll_left_);
        const std::size_t v1 = slice_from_cell(ln, scroll_left_ + cols);
        const std::size_t b_from = std::max(byte_at(ln, from), v0);
        const std::size_t b_to   = std::min(byte_at(ln, to), v1);
        Elements spans;
        const int pad = cell_at(ln, v0) - scroll_left_;
        if (pad > 0) spans.push_back(text(std::string(static_cast<std::size_t>(pad), ' ')));
        if (b_from > v0) spans.push_back(text(ln.substr(v0, b_from - v0)));
        std::string mid = b_to > b_from ? ln.substr(b_from, b_to - b_from) : std::string();
        const int sel_cell = cell_at(ln, byte_at(ln, from)) - scroll_left_;
        if (mid.empty() && from == to && sel_cell >= 0 && sel_cell < cols) mid = " ";
        if (!mid.empty()) spans.push_back(selection_style(text(mid)));
        if (b_to < v1) spans.push_back(text(ln.substr(b_to, v1 - b_to)));
        return hbox(std::move(spans));
    }

    Element render_status() {
        Position c = buffer_.cursor();
        std::string count =
            settings_.status_count == AppSettings::STATUS_COUNT_BYTES
                ? std::to_string(buffer_.byte_count()) + " bytes"
                : settings_.status_count == AppSettings::STATUS_COUNT_SIZE
                    ? human_readable_size(buffer_.byte_count())
                    : std::to_string(buffer_.char_count()) + " chars";
        // Horizontal position indicator, shown only when a visible line overflows.
        std::string hbar;
        const int cols = text_cols();
        const int max_left = max_scroll_left(cols);
        if (max_left > 0) {
            constexpr int kWidth = 8;
            const int total = cols + max_left;
            const int thumb = std::max(1, kWidth * cols / std::max(1, total));
            const int pos   = (kWidth - thumb) * scroll_left_ / std::max(1, max_left);
            for (int i = 0; i < kWidth; ++i)
                hbar += (i >= pos && i < pos + thumb) ? "█" : "░";
            hbar += "  ";
        }
        std::string right = count + "  " + hbar + newline_label(settings_.newline_style) +
                            "  Ln " + std::to_string(c.line + 1) +
                            ", Col " + std::to_string(c.col + 1) +
                            "  " + (buffer_.overwrite() ? "OVR" : "INS") + " ";
        return chrome_style(hbox({text(" " + status_msg_), filler(), text(right)}));
    }

    Element render_dialog() {
        Dimensions dim = Terminal::Size();
        int dw      = std::min(54, std::max(30, dim.dimx - 4));
        int inner_w = dw - 2;

        // Helper: render a single button, reflected for mouse hit-testing.
        auto make_btn = [](const std::string &label, bool active, Box &box) -> Element {
            Element e = ButtonOption::Simple().transform({label, false, active, active, -1});
            return e | reflect(box);
        };

        Elements rows;
        rows.push_back(text(""));  // top padding

        if (!dialog_body_.empty())
            rows.push_back(paragraph("  " + dialog_body_) | size(WIDTH, EQUAL, inner_w));

        if (dialog_kind_ == DialogKind::Prompt) {
            if (!dialog_body_.empty()) rows.push_back(text(""));
            const std::size_t cursor = std::min(dialog_cursor_, dialog_input_.size());
            std::string before = dialog_input_.substr(0, cursor);
            std::string after = dialog_input_.substr(cursor);
            if (dialog_mask_) {
                before.assign(Utf8ToGlyphs(before).size(), '*');
                after.assign(Utf8ToGlyphs(after).size(), '*');
            }
            auto marker = text(after.empty() ? " " : after.substr(0, utf8_next(after, 0)));
            if (dialog_btn_ == -1) marker = highlight_style(marker) | focusCursorBarBlinking;
            rows.push_back(hbox({text("  > "), text(before), marker,
                text(after.empty() ? "" : after.substr(utf8_next(after, 0))), filler()})
                | size(WIDTH, EQUAL, inner_w) | reflect(dialog_input_box_));
        }

        rows.push_back(text(""));  // spacer before buttons

        // Button row — centered.
        if (dialog_kind_ == DialogKind::Alert) {
            Element btn = make_btn("OK", true, dialog_btn0_box_) | focus;
            rows.push_back(hbox({filler(), std::move(btn), filler()}));
        } else if (dialog_kind_ == DialogKind::Confirm) {
            Element b0 = make_btn("Yes", dialog_btn_ == 0, dialog_btn0_box_);
            Element b1 = make_btn("No",  dialog_btn_ == 1, dialog_btn1_box_);
            if (dialog_btn_ == 0) b0 = std::move(b0) | focus;
            else                   b1 = std::move(b1) | focus;
            rows.push_back(hbox({filler(), std::move(b0), text("    "),
                                           std::move(b1), filler()}));
        } else {  // Prompt
            Element b0 = make_btn("OK",     dialog_btn_ == 0, dialog_btn0_box_);
            Element b1 = make_btn("Cancel", dialog_btn_ == 1, dialog_btn1_box_);
            rows.push_back(hbox({filler(), std::move(b0), text("    "),
                                           std::move(b1), filler()}));
        }

        rows.push_back(text(""));  // bottom padding

        Element dlg = chrome_style(window(text(" " + dialog_title_ + " "), vbox(std::move(rows)))
                    | clear_under
                    | size(WIDTH, EQUAL, dw));

        int dh = 9 + (!dialog_body_.empty() && dialog_kind_ == DialogKind::Prompt ? 2 : 0);
        int dx = std::max(0, (dim.dimx - dw) / 2);
        int dy = std::max(1, (dim.dimy - dh) / 2);
        return place_at(dx, dy, dlg);
    }

    // ---- File picker -------------------------------------------------------

    // Visible row count of the picker list (kept in sync between render/event).
    // Save mode reserves 3 extra rows for the name field and checkbox.
    int picker_view_h() const {
        Dimensions dim = Terminal::Size();
        int extra = (picker_mode_ == PickerMode::Save) ? 3 : 0;
        return std::min(14, std::max(3, dim.dimy - 12 - extra));
    }

    static bool ci_less(const std::string &a, const std::string &b) {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](unsigned char x, unsigned char y) {
                return std::tolower(x) < std::tolower(y);
            });
    }

    static bool iends_with(const std::string &s, const char *suf) {
        std::size_t n = std::strlen(suf);
        if (s.size() < n) return false;
        for (std::size_t i = 0; i < n; ++i)
            if (std::tolower((unsigned char)s[s.size() - n + i]) !=
                std::tolower((unsigned char)suf[i]))
                return false;
        return true;
    }

    // Make `name` an encrypted (.txx) name: a plain ".txt" becomes ".txx";
    // anything else gets ".txx" appended (e.g. main.cpp -> main.cpp.txx).
    static std::string add_txx(const std::string &name) {
        if (iends_with(name, ".txx")) return name;
        if (iends_with(name, ".txt")) return name.substr(0, name.size() - 4) + ".txx";
        return name + ".txx";
    }

    // Reverse of add_txx: drop ".txx"; restore ".txt" when nothing else remains
    // (notes.txx -> notes.txt) but keep an existing extension (main.cpp.txx -> main.cpp).
    static std::string strip_txx(const std::string &name) {
        if (!iends_with(name, ".txx")) return name;
        std::string base = name.substr(0, name.size() - 4);
        std::size_t slash = base.find_last_of("/\\");
        std::string fname = (slash == std::string::npos) ? base : base.substr(slash + 1);
        if (fname.find('.') != std::string::npos) return base;
        return base + ".txt";
    }

    bool name_is_encrypted() const { return has_txx_extension(save_name_); }

    // Populate picker_entries_ from `dir`: ".." (unless at root), directories,
    // then files — dotfiles hidden, directories first, case-insensitive sort.
    bool picker_load(const std::filesystem::path &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        // A bare filename has an empty parent_path(). Treat that as the
        // current directory, rather than asking directory_iterator to list "".
        fs::path requested = dir.empty() ? fs::current_path(ec) : dir;
        if (ec) {
            show_alert("Cannot Open", "Cannot determine current directory: " + ec.message());
            return false;
        }
        fs::path abs = fs::weakly_canonical(requested, ec);
        if (ec || abs.empty()) { ec.clear(); abs = fs::absolute(requested, ec); }
        if (ec) {
            show_alert("Cannot Open", "Cannot resolve directory: " + requested.string() + "\n" + ec.message());
            return false;
        }

        std::vector<PickerEntry> entries;
        ec.clear();
        fs::directory_iterator it(abs, ec);
        if (ec) {
            show_alert("Cannot Open", "Cannot read directory: " + abs.string() + "\n" + ec.message());
            return false;
        }
        for (fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;  // hide dotfiles
            std::error_code dec;
            entries.push_back({name, it->is_directory(dec)});
        }
        if (ec) {
            show_alert("Cannot Open", "Cannot read directory: " + abs.string() + "\n" + ec.message());
            return false;
        }
        std::sort(entries.begin(), entries.end(),
                  [](const PickerEntry &a, const PickerEntry &b) {
                      if (a.is_dir != b.is_dir) return a.is_dir;  // dirs first
                      return ci_less(a.name, b.name);
                  });
        if (abs.has_parent_path() && abs.parent_path() != abs)
            entries.insert(entries.begin(), PickerEntry{"..", true});

        picker_dir_     = abs;
        picker_entries_ = std::move(entries);
        picker_sel_     = 0;
        picker_scroll_  = 0;
        return true;
    }

    void picker_clamp_scroll(int view_h) {
        int total = (int)picker_entries_.size();
        if (picker_sel_ < picker_scroll_) picker_scroll_ = picker_sel_;
        if (picker_sel_ >= picker_scroll_ + view_h) picker_scroll_ = picker_sel_ - view_h + 1;
        int maxscroll = std::max(0, total - view_h);
        if (picker_scroll_ > maxscroll) picker_scroll_ = maxscroll;
        if (picker_scroll_ < 0) picker_scroll_ = 0;
    }

    Element render_picker_overlay() {
        Dimensions dim = Terminal::Size();
        bool save   = (picker_mode_ == PickerMode::Save);
        int dw      = std::min(64, std::max(36, dim.dimx - 4));
        int inner_w = dw - 2;
        int view_h  = picker_view_h();
        int total   = (int)picker_entries_.size();

        Elements rows;

        // Current directory path, right-truncated to fit.
        std::string dirstr = picker_dir_.string();
        if ((int)dirstr.size() > inner_w - 1)
            dirstr = "\xE2\x80\xA6" + dirstr.substr(dirstr.size() - (inner_w - 2));
        rows.push_back(text(" " + dirstr) | size(WIDTH, EQUAL, inner_w));
        rows.push_back(separator());

        picker_row_boxes_.assign(view_h, Box{-1, -1, -1, -1});
        bool list_focused = save_focus_ == FOCUS_LIST;
        if (total == 0) {
            rows.push_back(text("  (empty)") | size(WIDTH, EQUAL, inner_w));
            for (int i = 1; i < view_h; ++i) rows.push_back(text(""));
        } else {
            for (int i = 0; i < view_h; ++i) {
                int idx = picker_scroll_ + i;
                if (idx >= total) { rows.push_back(text("")); continue; }
                const PickerEntry &e = picker_entries_[idx];
                std::string label = (e.name == "..")        ? ".."
                                  : (e.is_dir ? e.name + "/" : e.name);
                Element row = text("  " + label) | size(WIDTH, EQUAL, inner_w);
                if (e.is_dir) row = row | color(theme_.accent);
                if (idx == picker_sel_ && list_focused) row = highlight_style(row);
                rows.push_back(row | reflect(picker_row_boxes_[i]));
            }
        }

        rows.push_back(separator());

        if (save) {
            // Filename input row with cursor at save_cursor_ when focused.
            Elements name_cells;
            name_cells.push_back(text(" Name: "));
            if (save_focus_ == FOCUS_NAME) {
                std::size_t cur = std::min(save_cursor_, save_name_.size());
                int clen = 0;
                if (cur < save_name_.size())
                    utf8_decode_next(save_name_.c_str() + cur,
                                     save_name_.c_str() + save_name_.size(), &clen);
                std::size_t csz = (clen > 0) ? (std::size_t)clen : 0;
                if (cur > 0)
                    name_cells.push_back(text(save_name_.substr(0, cur)));
                if (csz > 0) {
                    name_cells.push_back(
                        cursor_style(text(save_name_.substr(cur, csz))) | focusCursorBarBlinking);
                    if (cur + csz < save_name_.size())
                        name_cells.push_back(text(save_name_.substr(cur + csz)));
                } else {
                    name_cells.push_back(cursor_style(text(" ")) | focusCursorBarBlinking);
                }
            } else {
                name_cells.push_back(text(save_name_));
            }
            name_cells.push_back(filler());
            rows.push_back(hbox(std::move(name_cells))
                           | size(WIDTH, EQUAL, inner_w)
                           | reflect(picker_name_box_));

            // "Encrypted (.txx)" checkbox — mirrors whether the name ends in .txx.
            auto cbstate = EntryState{"Encrypted (.txx)", name_is_encrypted(),
                                      false, save_focus_ == FOCUS_ENC, -1};
            Element cb = hbox({text(" "),
                              CheckboxOption::Simple().transform(cbstate), filler()})
                       | size(WIDTH, EQUAL, inner_w);
            if (save_focus_ == FOCUS_ENC) cb = highlight_style(cb);
            rows.push_back(cb | reflect(picker_enc_box_));

            rows.push_back(separator());
            rows.push_back(text(" Tab focus  Enter save  Esc cancel")
                           | size(WIDTH, EQUAL, inner_w));
        } else {
            rows.push_back(text(" \xE2\x86\x91\xE2\x86\x93 move  Enter open  "
                                "\xE2\x86\x90 parent  Esc cancel")
                           | size(WIDTH, EQUAL, inner_w));
        }

        rows.push_back(separator());
        auto button = [&](const std::string &label, int focus_id, Box &box) {
            const bool active = save_focus_ == focus_id;
            auto result = ButtonOption::Simple().transform({label, false, active, active, -1});
            return result | reflect(box);
        };
        rows.push_back(hbox({filler(), button("Parent", FOCUS_PARENT, picker_parent_box_),
            text("  "), button(save ? "Save" : "Open", FOCUS_ACTION, picker_action_box_),
            text("  "), button("Cancel", FOCUS_CANCEL, picker_cancel_box_), filler()}));

        Element dlg = chrome_style(window(text(save ? " Save File " : " Open File "),
                             vbox(std::move(rows)))
                    | clear_under | size(WIDTH, EQUAL, dw));

        int dh = view_h + 10 + (save ? 3 : 0);  // +name +checkbox +separator
        int dx = std::max(0, (dim.dimx - dw) / 2);
        int dy = std::max(1, (dim.dimy - dh) / 2);
        return place_at(dx, dy, dlg);
    }

    void picker_move(int delta, int view_h) {
        int total = (int)picker_entries_.size();
        picker_sel_ = total ? std::min(std::max(picker_sel_ + delta, 0), total - 1) : 0;
        picker_clamp_scroll(view_h);
    }

    // Step back one UTF-8 codepoint from byte position `pos`.
    static std::size_t utf8_prev(const std::string &s, std::size_t pos) {
        if (pos == 0) return 0;
        do { --pos; } while (pos > 0 &&
            (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80);
        return pos;
    }

    // Step forward one UTF-8 codepoint from byte position `pos`.
    static std::size_t utf8_next(const std::string &s, std::size_t pos) {
        if (pos >= s.size()) return s.size();
        int len = 0;
        utf8_decode_next(s.c_str() + pos, s.c_str() + s.size(), &len);
        return pos + std::max(1, len);
    }

    static std::size_t byte_at_cell(const std::string &value, int cell) {
        std::size_t offset = 0;
        int width = 0;
        while (offset < value.size()) {
            const auto next = utf8_next(value, offset);
            const int glyph_width = string_width(std::string_view(value.data() + offset, next - offset));
            if (width + glyph_width > std::max(0, cell)) break;
            width += glyph_width;
            offset = next;
        }
        return offset;
    }

    void picker_activate() {
        if (picker_entries_.empty()) return;
        const PickerEntry entry = picker_entries_[picker_sel_];
        if (entry.name == "..") picker_load(picker_dir_.parent_path());
        else if (entry.is_dir) picker_load(picker_dir_ / entry.name);
        else if (picker_mode_ == PickerMode::Save) {
            save_name_ = entry.name;
            save_cursor_ = save_name_.size();
            save_focus_ = FOCUS_NAME;
        } else {
            const std::string full = (picker_dir_ / entry.name).string();
            if (open_path_flow(full)) picker_open_ = false;
        }
    }

    void name_backspace() {
        if (save_cursor_ == 0) return;
        std::size_t prev = utf8_prev(save_name_, save_cursor_);
        save_name_.erase(prev, save_cursor_ - prev);
        save_cursor_ = prev;
    }

    void name_insert(const std::string &ch) {
        save_name_.insert(save_cursor_, ch);
        save_cursor_ += ch.size();
    }

    void toggle_encrypted() {
        save_name_   = name_is_encrypted() ? strip_txx(save_name_) : add_txx(save_name_);
        save_cursor_ = std::min(save_cursor_, save_name_.size());
    }

    void commit_save() {
        if (save_name_.empty()) { show_alert("Save", "Enter a file name."); return; }
        std::filesystem::path full = picker_dir_ / save_name_;
        picker_open_ = false;
        filepath_ = full.string();
        syn_lang_ = syn_detect(filepath_);
        save_to_current_path();
    }

    bool picker_event(Event e) {
        int total  = (int)picker_entries_.size();
        int view_h = picker_view_h();
        bool save  = (picker_mode_ == PickerMode::Save);

        if (e == Event::Escape) { picker_open_ = false; return true; }

        // Mouse is handled first so clicks work regardless of keyboard focus.
        if (e.is_mouse()) {
            const Mouse &m = e.mouse();

            if (m.button == Mouse::WheelDown)    { picker_move(+1, view_h); return true; }
            if (m.button == Mouse::WheelUp)      { picker_move(-1, view_h); return true; }

            if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                auto hit = [&](const Box &b) {
                    return m.x >= b.x_min && m.x <= b.x_max &&
                           m.y >= b.y_min && m.y <= b.y_max;
                };

                if (hit(picker_cancel_box_)) { picker_open_ = false; return true; }
                if (hit(picker_parent_box_)) {
                    picker_load(picker_dir_.parent_path());
                    return true;
                }
                if (hit(picker_action_box_)) {
                    if (save) commit_save(); else picker_activate();
                    return true;
                }
                if (save && hit(picker_name_box_)) {
                    save_focus_        = FOCUS_NAME;
                    save_cursor_ = byte_at_cell(save_name_, m.x - picker_name_box_.x_min - 7);
                    picker_last_click_ = -1;
                    return true;
                }

                if (save && hit(picker_enc_box_)) {
                    toggle_encrypted();
                    picker_last_click_ = -1;
                    return true;
                }

                for (int i = 0; i < (int)picker_row_boxes_.size(); ++i) {
                    int idx = picker_scroll_ + i;
                    if (idx >= total) break;
                    if (!hit(picker_row_boxes_[i])) continue;

                    save_focus_ = FOCUS_LIST;

                    if (picker_sel_ == idx && picker_last_click_ == idx) {
                        picker_last_click_ = -1;
                        picker_activate();
                    } else {
                        picker_sel_        = idx;
                        picker_last_click_ = idx;
                        picker_clamp_scroll(view_h);
                    }
                    return true;
                }

                picker_last_click_ = -1;
            }
            return true;
        }

        if (e == Event::Tab || e == Event::TabReverse) {
            const std::vector<int> controls = save
                ? std::vector<int>{FOCUS_LIST, FOCUS_NAME, FOCUS_ENC, FOCUS_PARENT, FOCUS_ACTION, FOCUS_CANCEL}
                : std::vector<int>{FOCUS_LIST, FOCUS_PARENT, FOCUS_ACTION, FOCUS_CANCEL};
            auto it = std::find(controls.begin(), controls.end(), save_focus_);
            int index = it == controls.end() ? 0 : static_cast<int>(it - controls.begin());
            index = (index + (e == Event::Tab ? 1 : static_cast<int>(controls.size()) - 1)) % controls.size();
            save_focus_ = controls[index];
            return true;
        }
        if (save_focus_ >= FOCUS_PARENT) {
            if (e == Event::Return || e.input() == " ") {
                if (save_focus_ == FOCUS_CANCEL) picker_open_ = false;
                else if (save_focus_ == FOCUS_PARENT) picker_load(picker_dir_.parent_path());
                else if (save) commit_save();
                else picker_activate();
            }
            return true;
        }

        // Save mode — filename input has focus.
        if (save && save_focus_ == FOCUS_NAME) {
            if (e == Event::Return)     { commit_save();                              return true; }
            if (e == Event::ArrowUp)    { save_focus_ = FOCUS_LIST;                  return true; }
            if (e == Event::ArrowDown)  { save_focus_ = FOCUS_ENC;                   return true; }
            if (e == Event::ArrowLeft)  { save_cursor_ = utf8_prev(save_name_, save_cursor_); return true; }
            if (e == Event::ArrowRight) { save_cursor_ = utf8_next(save_name_, save_cursor_); return true; }
            if (e == Event::Home)       { save_cursor_ = 0;                           return true; }
            if (e == Event::End)        { save_cursor_ = save_name_.size();           return true; }
            if (e == Event::Backspace)  { name_backspace();                           return true; }
            if (e.is_character())       { name_insert(e.character());                 return true; }
            return true;
        }

        // Save mode — encrypted checkbox has focus.
        if (save && save_focus_ == FOCUS_ENC) {
            if (e == Event::Return || e.input() == " ") { toggle_encrypted(); return true; }
            if (e == Event::ArrowUp)    { save_focus_ = FOCUS_NAME; return true; }
            if (e == Event::ArrowDown)  { save_focus_ = FOCUS_LIST; return true; }
            return true;
        }

        // List focus.
        if (e == Event::ArrowDown || (!save && e.input() == "j")) { picker_move(+1, view_h); return true; }
        if (e == Event::ArrowUp   || (!save && e.input() == "k")) { picker_move(-1, view_h); return true; }
        if (e == Event::PageDown) { picker_move(view_h, view_h);  return true; }
        if (e == Event::PageUp)   { picker_move(-view_h, view_h); return true; }
        if (e == Event::Home) { picker_sel_ = 0; picker_clamp_scroll(view_h); return true; }
        if (e == Event::End)  {
            if (total) picker_sel_ = total - 1;
            picker_clamp_scroll(view_h);
            return true;
        }

        if (e == Event::ArrowLeft || e == Event::Backspace) {
            picker_load(picker_dir_.parent_path());
            return true;
        }

        if (e == Event::ArrowRight) {
            if (total) {
                const PickerEntry &entry = picker_entries_[picker_sel_];
                if (entry.name == "..")  picker_load(picker_dir_.parent_path());
                else if (entry.is_dir)   picker_load(picker_dir_ / entry.name);
            }
            return true;
        }

        if (e == Event::Return) { picker_activate(); return true; }

        return true;  // modal swallows all other input
    }

    // ---- Menu event handling -----------------------------------------------

    // Next/prev selectable item (wraps, skips separators).
    static int next_item(const std::vector<MenuEntry> &v, int cur) {
        int n = (int)v.size();
        if (!n) return -1;
        for (int i = 1; i <= n; ++i) {
            int idx = (cur + i) % n;
            if (!v[idx].label.empty()) return idx;
        }
        return cur;
    }
    static int prev_item(const std::vector<MenuEntry> &v, int cur) {
        int n = (int)v.size();
        if (!n) return -1;
        if (cur < 0) cur = 0;
        for (int i = 1; i <= n; ++i) {
            int idx = (cur - i + n) % n;
            if (!v[idx].label.empty()) return idx;
        }
        return cur;
    }

    void open_top(int idx) {
        menu_open_   = idx;
        menu_item_   = next_item(menus_[idx].items, -1);  // pre-select first item
        menu_sub_    = -1;
        menu_in_sub_ = false;
    }

    void close_menu() {
        menu_open_       = -1;
        menu_item_       = -1;
        menu_sub_        = -1;
        menu_in_sub_     = false;
        menu_mouse_held_ = false;
    }

    void run_item(std::function<void()> act) {
        close_menu();
        act();
    }

    bool menu_event(Event e) {
        if (e.is_mouse()) return menu_mouse_event(e);

        if (e == Event::Escape) { close_menu(); return true; }

        auto &items = menus_[menu_open_].items;

        if (menu_in_sub_) {
            const auto &sub = items[menu_item_].sub;
            if (e == Event::ArrowDown)  { menu_sub_ = next_item(sub, menu_sub_); return true; }
            if (e == Event::ArrowUp)    { menu_sub_ = prev_item(sub, menu_sub_); return true; }
            if (e == Event::ArrowLeft)  { menu_in_sub_ = false; return true; }
            if (e == Event::Return) {
                if (menu_sub_ >= 0 && sub[menu_sub_].action)
                    run_item(sub[menu_sub_].action);
                return true;
            }
            return true;
        }

        if (e == Event::ArrowDown) { menu_item_ = next_item(items, menu_item_); return true; }
        if (e == Event::ArrowUp)   { menu_item_ = prev_item(items, menu_item_); return true; }
        if (e == Event::ArrowLeft) {
            menu_open_ = (menu_open_ + (int)menus_.size() - 1) % (int)menus_.size();
            menu_item_ = -1; menu_in_sub_ = false;
            return true;
        }
        if (e == Event::ArrowRight) {
            if (menu_item_ >= 0 && !items[menu_item_].sub.empty()) {
                menu_in_sub_ = true;
                menu_sub_ = next_item(items[menu_item_].sub, -1);
            } else {
                menu_open_ = (menu_open_ + 1) % (int)menus_.size();
                menu_item_ = -1; menu_in_sub_ = false;
            }
            return true;
        }
        if (e == Event::Return) {
            if (menu_item_ < 0 || menu_item_ >= (int)items.size()) return true;
            const auto &item = items[menu_item_];
            if (!item.sub.empty()) {
                menu_in_sub_ = true;
                menu_sub_ = next_item(item.sub, -1);
            } else if (item.action) {
                run_item(item.action);
            }
            return true;
        }
        // Alt+letter: switch to the matching top-level menu.
        {
            int top = alt_menu_at(e.input());
            if (top >= 0) { open_top(top); return true; }
        }
        // F9: close menu (mirrors the open shortcut).
        if (e == Event::F9) { close_menu(); return true; }

        return true;  // swallow all keys while menu is open
    }

    // Which top-level menu title contains screen column x.
    int top_menu_at(int x) const {
        for (int i = 0; i < (int)menus_.size(); ++i)
            if (x >= menus_[i].title_box.x_min && x <= menus_[i].title_box.x_max)
                return i;
        return -1;
    }

    // Which top-level menu has `seq` as its Alt+first-letter shortcut.
    // Alt+letter arrives as ESC + letter after the 50 ms FTXUI flush timeout.
    int alt_menu_at(const std::string &seq) const {
        if (seq.size() != 2 || seq[0] != '\x1b') return -1;
        char c = static_cast<char>(tolower(static_cast<unsigned char>(seq[1])));
        for (int i = 0; i < (int)menus_.size(); ++i) {
            if (!menus_[i].title.empty() &&
                tolower(static_cast<unsigned char>(menus_[i].title[0])) == c)
                return i;
        }
        return -1;
    }

    // Update item highlights based on mouse position during drag.
    void track_mouse(int mx, int my) {
        if (menu_open_ < 0) return;
        const auto &menu  = menus_[menu_open_];
        const auto &items = menu.items;
        int iw  = items_inner_width(items);
        int ddw = iw + 2;
        int ddx = menu.title_box.x_min;

        // Submenu area first.
        if (menu_item_ >= 0 && menu_item_ < (int)items.size() &&
            !items[menu_item_].sub.empty()) {
            int sub_x = ddx + ddw;
            int sub_y = 1 + menu_item_;
            const auto &sub = items[menu_item_].sub;
            int siw = items_inner_width(sub);
            if (mx >= sub_x && mx < sub_x + siw + 2 && my > sub_y) {
                int idx = my - sub_y - 1;
                if (idx >= 0 && idx < (int)sub.size() && !sub[idx].label.empty()) {
                    menu_sub_    = idx;
                    menu_in_sub_ = true;
                    return;
                }
            }
        }

        // Main dropdown.
        if (mx >= ddx && mx < ddx + ddw && my >= 2) {
            int idx = my - 2;
            if (idx >= 0 && idx < (int)items.size() && !items[idx].label.empty()) {
                menu_item_   = idx;
                menu_in_sub_ = false;
                menu_sub_    = -1;
            }
        }
    }

    // Hit-test (mx,my) and execute the item there.  Used for both Pressed and
    // Released so execution works even when the terminal drops Release events.
    void execute_click(int mx, int my) {
        if (menu_open_ < 0) return;
        const auto &menu  = menus_[menu_open_];
        const auto &items = menu.items;
        int iw  = items_inner_width(items);
        int ddx = menu.title_box.x_min;
        int ddw = iw + 2;

        if (my == 0) return;  // menu-bar clicks handled in Pressed

        // Submenu (if one is currently visible).
        if (menu_item_ >= 0 && menu_item_ < (int)items.size() &&
            !items[menu_item_].sub.empty()) {
            const auto &sub = items[menu_item_].sub;
            int siw   = items_inner_width(sub);
            int sub_x = ddx + ddw;
            int sub_y = 1 + menu_item_;
            if (mx >= sub_x && mx < sub_x + siw + 2 && my > sub_y) {
                int idx = my - sub_y - 1;
                if (idx >= 0 && idx < (int)sub.size() && !sub[idx].label.empty()) {
                    if (sub[idx].action) run_item(sub[idx].action);
                    else close_menu();
                    return;
                }
            }
        }

        // Main dropdown.
        if (mx >= ddx && mx < ddx + ddw && my >= 2) {
            int idx = my - 2;
            if (idx >= 0 && idx < (int)items.size() && !items[idx].label.empty()) {
                const auto &item = items[idx];
                if (!item.sub.empty()) {
                    // Submenu trigger: show the submenu, don't close.
                    menu_item_   = idx;
                    menu_in_sub_ = false;
                    return;
                }
                if (item.action) { run_item(item.action); return; }
            }
        }

        // Clicked outside every panel.
        close_menu();
    }

    bool menu_mouse_event(Event e) {
        const Mouse &m = e.mouse();

        if (m.button == Mouse::WheelUp   && !menu_in_sub_) {
            if (menu_open_ >= 0) menu_item_ = prev_item(menus_[menu_open_].items, menu_item_);
            return true;
        }
        if (m.button == Mouse::WheelDown && !menu_in_sub_) {
            if (menu_open_ >= 0) menu_item_ = next_item(menus_[menu_open_].items, menu_item_);
            return true;
        }

        if (m.button != Mouse::Left) return true;

        if (m.motion == Mouse::Released) {
            menu_mouse_held_ = false;
            // Execute via fresh hit-test; handles drag-and-release correctly.
            execute_click(m.x, m.y);
            return true;
        }

        // Pressed (initial click or SGR drag event).
        if (m.y == 0) {
            // Menu bar: toggle or switch top-level menu.
            int top = top_menu_at(m.x);
            if (!menu_mouse_held_) {
                menu_mouse_held_ = true;
                if (top >= 0) {
                    if (top == menu_open_) close_menu();
                    else open_top(top);
                } else {
                    close_menu();
                }
            } else {
                if (top >= 0 && top != menu_open_) open_top(top);
            }
        } else {
            if (!menu_mouse_held_) {
                // Fresh click directly on a dropdown item (not a drag from the title).
                // Execute immediately — don't rely on a Released event arriving.
                menu_mouse_held_ = true;
                execute_click(m.x, m.y);
            } else {
                // Drag: update the highlight only; execute on Released.
                track_mouse(m.x, m.y);
            }
        }

        return true;
    }

    // ---- Editor event (menu closed, no prompt) -----------------------------
    bool editor_event(Event e) {
        if (e == Event::F3 || e.input() == "\x1b[13~") { repeat_find(false); return true; }
        if (e == Event::F2 || e.input() == "\x1b[12~") { repeat_find(true); return true; }

        // F9 / F10 open the File menu (F9 = mcedit convention; F10 works in
        // terminals that don't intercept it).
        if (e == Event::F9 || e == Event::F10) { open_top(0); return true; }

        // Alt alone (bare ESC after 50 ms FTXUI timeout) opens the File menu,
        // mirroring the Windows/GTK convention of focusing the menu bar.
        if (e == Event::Escape) { open_top(0); return true; }

        // Alt+first-letter opens the matching top-level menu.
        {
            int top = alt_menu_at(e.input());
            if (top >= 0) { open_top(top); return true; }
        }

        const std::string &in = e.input();
        // Alt+Z / Alt+Y mirror Ctrl+Z / Ctrl+Y for convenience.
        if (in == "\x1bz") { edit([&]{ buffer_.undo(); }); return true; }
        if (in == "\x1by") { edit([&]{ buffer_.redo(); }); return true; }
        if (in.size() == 1) {
            switch (static_cast<unsigned char>(in[0])) {
                case 0x0e: cmd_new(); return true;
                case 0x0f: cmd_open(); return true;
                case 0x13: cmd_save(false); return true;
                case 0x11: cmd_quit(); return true;
                case 0x1a: edit([&]{ buffer_.undo(); }); return true;
                case 0x19: edit([&]{ buffer_.redo(); }); return true;
                case 0x18: cmd_cut(); return true;
                case 0x03: cmd_copy(); return true;
                case 0x16: cmd_paste(); return true;
                case 0x01: buffer_.select_all(); return true;
                case 0x06: cmd_find(); return true;
                case 0x12: cmd_replace(); return true;
                case 0x07: cmd_goto(); return true;
                case 0x04: edit([&]{ buffer_.duplicate_line(); }); return true;
                case 0x0b: edit([&]{ buffer_.remove_line(); }); return true;
                default: break;
            }
        }

        if (in == "\x1b[1;2A") { buffer_.move_up(true);    return true; }
        if (in == "\x1b[1;2B") { buffer_.move_down(true);  return true; }
        if (in == "\x1b[1;2C") { buffer_.move_right(true); return true; }
        if (in == "\x1b[1;2D") { buffer_.move_left(true);  return true; }
        if (in == "\x1b[1;2H") { buffer_.move_home(true);  return true; }
        if (in == "\x1b[1;2F") { buffer_.move_end(true);   return true; }

        if (e == Event::ArrowUp)    { buffer_.move_up(false);    return true; }
        if (e == Event::ArrowDown)  { buffer_.move_down(false);  return true; }
        if (e == Event::ArrowLeft)  { buffer_.move_left(false);  return true; }
        if (e == Event::ArrowRight) { buffer_.move_right(false); return true; }
        if (e == Event::Home)       { buffer_.move_home(false);  return true; }
        if (e == Event::End)        { buffer_.move_end(false);   return true; }
        if (e == Event::PageUp)     { buffer_.move_page(-page_step(), false); return true; }
        if (e == Event::PageDown)   { buffer_.move_page( page_step(), false); return true; }
        if (in == "\x1b[2~")        { buffer_.toggle_overwrite(); return true; }

        if (e == Event::Return)    { edit([&]{ buffer_.insert_newline(); }); return true; }
        if (e == Event::Backspace) { edit([&]{ buffer_.backspace(); });      return true; }
        if (e == Event::Delete)    { edit([&]{ buffer_.del(); });            return true; }
        if (e == Event::Tab) {
            edit([&]{ buffer_.indent(settings_.use_spaces(), settings_.indent_width); });
            return true;
        }
        if (e == Event::TabReverse) {
            edit([&]{ buffer_.outdent(settings_.indent_width); });
            return true;
        }

        if (e.is_mouse()) return mouse_event(e);

        if (e.is_character()) { edit([&]{ buffer_.insert_text(e.character()); }); return true; }
        return false;
    }

    bool mouse_event(Event e) {
        const Mouse &m = e.mouse();
        if (m.button == Mouse::WheelUp)   { buffer_.move_page(-3, false); return true; }
        if (m.button == Mouse::WheelDown) { buffer_.move_page( 3, false); return true; }
        if (m.button == Mouse::WheelLeft)  {
            for (int i = 0; i < 4; ++i) buffer_.move_left(false);
            return true;
        }
        if (m.button == Mouse::WheelRight) {
            for (int i = 0; i < 4; ++i) buffer_.move_right(false);
            return true;
        }
        if (m.button == Mouse::Left) {
            // A release after toggling a menu closed belongs to the same
            // click; only a press on a closed menu should open it again.
            if (m.y == 0) {
                if (m.motion == Mouse::Pressed) {
                    int top = top_menu_at(m.x);
                    if (top >= 0) {
                        menu_mouse_held_ = true;
                        open_top(top);
                    }
                }
                return true;
            }
            const auto dim = Terminal::Size();
            const int rows = std::max(1, dim.dimy - 2);
            if (settings_.vertical_scrollbar && buffer_.line_count() > rows &&
                m.x == dim.dimx - 1 && m.y >= 1 && m.y <= rows) {
                if (m.motion != Mouse::Released) {
                    const int target = (m.y - 1) * (buffer_.line_count() - rows) /
                                       std::max(1, rows - 1);
                    scroll_top_ = target;
                    const int cursor_line = std::clamp(buffer_.cursor().line, target, target + rows - 1);
                    buffer_.set_cursor({cursor_line, buffer_.cursor().col});
                    buffer_.clear_selection();
                }
                mouse_down_ = false;
                return true;
            }
            // Editor area: row 1 = first editor line.
            int line = scroll_top_ + m.y - 1;
            line = std::clamp(line, 0, buffer_.line_count() - 1);
            const int col = col_at(buffer_.line(line), byte_at_cell(buffer_.line(line), m.x + scroll_left_));
            Position p{line, col};
            if (m.motion == Mouse::Released) {
                mouse_down_ = false;
            } else if (!mouse_down_) {
                mouse_down_ = true;
                buffer_.set_cursor(p);
                buffer_.clear_selection();
            } else {
                buffer_.extend_cursor(p);
            }
            return true;
        }
        return false;
    }

    int page_step() { return std::max(1, Terminal::Size().dimy - 3); }

    // ---- Dialog (modal prompt / confirm / alert) ---------------------------
    void show_prompt(std::string title, bool mask, std::string initial,
                     std::function<void(bool, std::string)> cb) {
        dialog_kind_  = DialogKind::Prompt;
        dialog_btn_   = -1;
        dialog_title_ = std::move(title);
        dialog_body_  = {};
        dialog_mask_  = mask;
        dialog_input_ = std::move(initial);
        dialog_cursor_ = dialog_input_.size();
        dialog_cb_    = std::move(cb);
    }

    void show_confirm(std::string title, std::string body,
                      std::function<void(bool)> cb) {
        dialog_kind_  = DialogKind::Confirm;
        dialog_btn_   = 1;  // default focus on "No" (safer)
        dialog_title_ = std::move(title);
        dialog_body_  = std::move(body);
        dialog_mask_  = false;
        dialog_input_ = {};
        dialog_cb_    = [cb = std::move(cb)](bool ok, std::string){ cb(ok); };
    }

    void show_alert(std::string title, std::string body) {
        TuiPluginDialogs dialogs(std::move(title), theme_);
        plugins::DialogRequest request{};
        request.kind = plugins::DialogKind::Message;
        request.prompt = std::move(body);
        dialogs.show(request);
    }

    bool dialog_event(Event e) {
        // Mouse: hit-test the rendered button boxes.
        if (e.is_mouse()) {
            const Mouse &m = e.mouse();
            if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                auto hit = [&](const Box &b) {
                    return m.x >= b.x_min && m.x <= b.x_max &&
                           m.y >= b.y_min && m.y <= b.y_max;
                };
                if (dialog_kind_ == DialogKind::Prompt && hit(dialog_input_box_)) {
                    dialog_btn_ = -1;
                    if (dialog_mask_) {
                        int count = std::max(0, m.x - dialog_input_box_.x_min - 4);
                        dialog_cursor_ = 0;
                        while (count-- > 0 && dialog_cursor_ < dialog_input_.size())
                            dialog_cursor_ = utf8_next(dialog_input_, dialog_cursor_);
                    } else {
                        dialog_cursor_ = byte_at_cell(dialog_input_, m.x - dialog_input_box_.x_min - 4);
                    }
                    return true;
                }
                if (hit(dialog_btn0_box_)) { close_dialog(true);  return true; }
                if (dialog_kind_ != DialogKind::Alert && hit(dialog_btn1_box_))
                    { close_dialog(false); return true; }
            }
            return true;
        }

        if (dialog_kind_ == DialogKind::Alert) {
            if (e == Event::Escape || e == Event::Return) close_dialog(false);
            return true;
        }

        if (dialog_kind_ == DialogKind::Confirm) {
            // Arrow keys / Tab move focus between Yes and No.
            if (e == Event::ArrowLeft  || e == Event::ArrowRight ||
                e == Event::Tab        || e == Event::TabReverse)
                { dialog_btn_ ^= 1; return true; }
            if (e == Event::Return)
                { close_dialog(dialog_btn_ == 0); return true; }
            if (e == Event::Escape)
                { close_dialog(false); return true; }
            const std::string &in = e.input();
            if (in.size() == 1 && (in[0] == 'y' || in[0] == 'Y'))
                { close_dialog(true);  return true; }
            if (in.size() == 1 && (in[0] == 'n' || in[0] == 'N'))
                { close_dialog(false); return true; }
            return true;
        }

        if (e == Event::Escape) { close_dialog(false); return true; }
        if (e == Event::Tab || e == Event::TabReverse) {
            dialog_btn_ = (dialog_btn_ + (e == Event::Tab ? 2 : 3)) % 3 - 1;
            return true;
        }
        if (e == Event::Return) { close_dialog(dialog_btn_ != 1); return true; }
        if (dialog_btn_ != -1) {
            if (e.input() == " ") close_dialog(dialog_btn_ == 0);
            return true;
        }
        if (e == Event::ArrowLeft) { dialog_cursor_ = utf8_prev(dialog_input_, dialog_cursor_); return true; }
        if (e == Event::ArrowRight) { dialog_cursor_ = utf8_next(dialog_input_, dialog_cursor_); return true; }
        if (e == Event::Home) { dialog_cursor_ = 0; return true; }
        if (e == Event::End) { dialog_cursor_ = dialog_input_.size(); return true; }
        if (e == Event::Backspace) {
            const auto previous = utf8_prev(dialog_input_, dialog_cursor_);
            dialog_input_.erase(previous, dialog_cursor_ - previous);
            dialog_cursor_ = previous;
            return true;
        }
        if (e == Event::Delete) {
            dialog_input_.erase(dialog_cursor_, utf8_next(dialog_input_, dialog_cursor_) - dialog_cursor_);
            return true;
        }
        if (e.is_character()) {
            dialog_input_.insert(dialog_cursor_, e.character());
            dialog_cursor_ += e.character().size();
            return true;
        }
        return true;
    }

    void close_dialog(bool accepted) {
        auto cb  = dialog_cb_;
        auto val = dialog_input_;
        dialog_kind_  = DialogKind::None;
        dialog_cb_    = nullptr;
        dialog_input_.clear();
        if (cb) cb(accepted, val);
    }

    // ---- Lua plugins -------------------------------------------------------
    static MenuEntry plugin_entry(std::string label, std::function<void()> action = {}) {
        MenuEntry entry{};
        entry.label = std::move(label); entry.action = std::move(action);
        return entry;
    }

    std::vector<MenuEntry> plugin_folder_entries(const std::vector<std::string> &prefix,
                                                bool submenu) {
        std::map<std::string, std::vector<std::string>> folders;
        std::vector<std::size_t> commands;
        for (std::size_t i = 0; i < plugin_catalog_.commands.size(); ++i) {
            const auto &command = plugin_catalog_.commands[i];
            if (command.folders.size() < prefix.size() ||
                !std::equal(prefix.begin(), prefix.end(), command.folders.begin())) continue;
            if (command.folders.size() == prefix.size()) commands.push_back(i);
            else {
                auto path = prefix; path.push_back(command.folders[prefix.size()]);
                folders[path.back()] = path;
            }
        }
        std::vector<MenuEntry> entries;
        for (const auto &folder : folders) {
            auto entry = plugin_entry(folder.first + (submenu ? "" : "/"),
                [this, path = folder.second] { browse_plugin_folder(path); });
            if (submenu) entry.sub = plugin_folder_entries(folder.second, false);
            entries.push_back(std::move(entry));
        }
        for (auto index : commands)
            entries.push_back(plugin_entry(plugin_catalog_.commands[index].label,
                [this, index] { run_plugin(index); }));
        return entries;
    }

    std::vector<MenuEntry> plugin_menu_entries() {
        std::vector<MenuEntry> entries{
            plugin_entry("Install Default Plugins", [this] { install_default_plugins(); }),
            plugin_entry("Reload Plugins", [this] { reload_plugins(true); }),
            plugin_entry("Show Plugins Folder", [this] {
                plugin_message(plugin_root_.empty() ? "Could not determine the plugin directory."
                                                   : plugin_root_.u8string());
            }), plugin_entry("")};
        auto commands = plugin_folder_entries({}, true);
        entries.insert(entries.end(), commands.begin(), commands.end());
        if (commands.empty()) entries.push_back(plugin_entry("No plugins installed"));
        if (!plugin_catalog_.errors.empty()) entries.push_back(plugin_entry("Show discovery errors",
            [this] { plugin_errors(); }));
        return entries;
    }

    void plugin_message(const std::string &message) {
        TuiPluginDialogs dialogs("Plugins", theme_);
        plugins::DialogRequest request{};
        request.kind = plugins::DialogKind::Message; request.prompt = message;
        dialogs.show(request);
    }

    void plugin_errors() {
        std::string errors;
        for (const auto &error : plugin_catalog_.errors) errors += error + "\n";
        if (!errors.empty()) plugin_message(errors);
    }

    void reload_plugins(bool report_errors) {
        if (plugin_running_) return;
        close_menu();
        plugin_catalog_ = plugins::discover(plugin_root_);
        for (auto &menu : menus_) if (menu.title == "Plugins") menu.items = plugin_menu_entries();
        set_status("Loaded " + std::to_string(plugin_catalog_.commands.size()) + " plugins");
        if (report_errors) plugin_errors();
    }

    void install_default_plugins() {
        if (plugin_running_) return;
        auto result = plugins::install_defaults(plugins::default_directory(plugins::executable_path()), plugin_root_);
        reload_plugins(false);
        std::string message = "Installed " + std::to_string(result.installed) + " plugins; kept " +
            std::to_string(result.skipped.size()) + " existing files.";
        for (const auto &path : result.skipped) message += "\nKept: " + path;
        for (const auto &error : result.errors) message += "\n" + error;
        plugin_message(message);
    }

    // Existing dropdowns have one submenu level. A modal browser handles deeper folders.
    void browse_plugin_folder(std::vector<std::string> path) {
        while (true) {
            auto entries = plugin_folder_entries(path, false);
            plugins::DialogRequest request{};
            request.kind = plugins::DialogKind::Choose;
            request.prompt = "Choose a plugin or folder:";
            if (!path.empty()) request.choices.push_back(".. (Back)");
            for (const auto &entry : entries) request.choices.push_back(entry.label);
            if (request.choices.empty()) return;
            TuiPluginDialogs dialogs("Plugins", theme_);
            auto reply = dialogs.show(request);
            if (!reply.accepted) return;
            if (!path.empty() && reply.choice == 1) { path.pop_back(); continue; }
            auto index = static_cast<std::size_t>(reply.choice - 1 - (!path.empty() ? 1 : 0));
            if (index >= entries.size()) return;
            if (!entries[index].label.empty() && entries[index].label.back() == '/') {
                path.push_back(entries[index].label.substr(0, entries[index].label.size() - 1));
                continue;
            }
            if (entries[index].action) entries[index].action();
            return;
        }
    }

    void run_plugin(std::size_t index) {
        if (plugin_running_ || index >= plugin_catalog_.commands.size()) return;
        const auto &command = plugin_catalog_.commands[index];
        std::string source, error;
        if (!plugins::read_script(plugin_root_, command, source, error)) {
            plugin_message(error); return;
        }
        auto document = plugin_document(buffer_);
        document.indent_width = settings_.indent_width;
        document.indent_spaces = settings_.use_spaces();
        TuiPluginDialogs dialogs("Plugin: " + command.label, theme_);
        plugin_running_ = true;
        auto result = plugins::run(source, "@" + command.path.filename().u8string(), document, dialogs);
        plugin_running_ = false;
        if (!source.empty()) txx_secure_zero(source.data(), source.size());
        if (result.status == plugins::Status::Success) {
            if (apply_plugin_document(buffer_, result.document)) dirty_ = true;
            set_status("Plugin completed: " + command.label);
        } else if (result.status == plugins::Status::Error) plugin_message(result.error);
        else set_status("Plugin cancelled: " + command.label);
    }

    // ---- Commands ----------------------------------------------------------
    void edit(const std::function<void()> &fn) { fn(); dirty_ = true; }

    void transform(const std::function<std::string(const std::string &)> &fn) {
        std::string updated = fn(buffer_.text());
        buffer_.select_all();
        edit([&]{ buffer_.insert_text(updated); });
        buffer_.set_cursor(Position{0, 0});
        set_status("Transform applied");
    }

    void set_newline(txx_newline_style s) {
        settings_.newline_style = s;
        dirty_ = true;
        config_save(settings_);
        set_status(std::string("Line endings: ") + newline_label(s));
    }

    void cmd_new() {
        confirm_discard([this]{
            buffer_.set_text(""); filepath_.clear(); password_.clear();
            dirty_ = false; syn_lang_ = SynLang::None; scroll_top_ = 0; scroll_left_ = 0;
            set_status("New document");
        });
    }

    void cmd_open() {
        confirm_discard([this]{
            std::filesystem::path start = filepath_.empty()
                ? std::filesystem::current_path()
                : std::filesystem::path(filepath_).parent_path();
            picker_mode_ = PickerMode::Open;
            save_focus_ = FOCUS_LIST;
            picker_open_ = picker_load(start);
        });
    }

    static bool has_txx_magic(const uint8_t *data, std::size_t len) {
        static const uint8_t kMagic[8] = {'T','X','X','1', 0x0d, 0x0a, 0x1a, 0x0a};
        return len >= 8 && memcmp(data, kMagic, 8) == 0;
    }

    static bool has_txx_extension(const std::string &path) {
        if (path.size() < 4) return false;
        std::string tail = path.substr(path.size() - 4);
        for (char &c : tail) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return tail == ".txx";
    }

    // Returns true once the file has loaded or a password prompt was launched;
    // false (with an alert shown) when the file is unreadable or not text — the
    // file picker uses this to decide whether to stay open.
    bool open_path_flow(const std::string &path) {
        txx_buffer raw; txx_buffer_init(&raw);
        txx_status s = txx_read_file_limited(path.c_str(), kMaxFileBytes, &raw);
        if (s != TXX_OK) {
            txx_buffer_secure_free(&raw);
            show_alert("Error", std::string("Open failed: ") + txx_status_string(s));
            return false;
        }

        bool is_txx = has_txx_extension(path) || has_txx_magic(raw.data, raw.len);

        if (!is_txx) {
            txx_buffer utf8; txx_buffer_init(&utf8);
            txx_text_info info; txx_text_info_init(&info);
            txx_status ts = txx_text_decode_to_utf8(raw.data, raw.len, &utf8, &info);
            txx_buffer_secure_free(&raw);
            if (ts != TXX_OK) {
                txx_buffer_secure_free(&utf8);
                show_alert("Cannot Open", base_name(path) + " is not a text file.");
                return false;
            }
            std::string plain(reinterpret_cast<const char *>(utf8.data), utf8.len);
            txx_buffer_free(&utf8);
            int mixed = 0;
            txx_newline_style nl = txx_text_detect_newlines(
                reinterpret_cast<const uint8_t *>(plain.data()), plain.size(), &mixed);
            if (nl != TXX_NEWLINE_UNKNOWN) settings_.newline_style = nl;
            buffer_.set_text(plain);
            filepath_ = path; password_.clear(); dirty_ = false; scroll_top_ = 0; scroll_left_ = 0;
            syn_lang_ = syn_detect(path);
            set_status("Opened " + base_name(path) + " (plain text)");
            return true;
        }

        std::string encoded(reinterpret_cast<const char *>(raw.data), raw.len);
        txx_buffer_secure_free(&raw);
        show_prompt("Password", true, "", [this, encoded, path](bool ok, std::string pw){
            if (!ok) return;
            txx_buffer out; txx_buffer_init(&out);
            txx_status st = txx_decode_text(
                reinterpret_cast<const uint8_t *>(encoded.data()), encoded.size(),
                pw.c_str(), &out);
            if (st != TXX_OK) {
                txx_buffer_secure_free(&out);
                show_alert("Error", std::string("Decode failed: ") + txx_status_string(st));
                return;
            }
            std::string plain(reinterpret_cast<const char *>(out.data), out.len);
            txx_buffer_secure_free(&out);
            int mixed = 0;
            txx_newline_style nl = txx_text_detect_newlines(
                reinterpret_cast<const uint8_t *>(plain.data()), plain.size(), &mixed);
            if (nl != TXX_NEWLINE_UNKNOWN) settings_.newline_style = nl;
            buffer_.set_text(plain);
            filepath_ = path; password_ = pw; dirty_ = false; scroll_top_ = 0; scroll_left_ = 0;
            syn_lang_ = syn_detect(path);
            set_status("Opened " + base_name(path));
        });
        return true;  // password prompt launched
    }

    void cmd_save(bool save_as) {
        if (filepath_.empty() || save_as) { open_save_picker(); return; }
        save_to_current_path();
    }

    void open_save_picker() {
        std::filesystem::path start = filepath_.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(filepath_).parent_path();
        if (start.empty()) start = std::filesystem::current_path();
        picker_mode_ = PickerMode::Save;
        save_name_   = base_name(filepath_);
        save_cursor_ = save_name_.size();
        save_focus_  = FOCUS_NAME;
        picker_open_ = picker_load(start);
    }

    // Write to filepath_. The .txx extension decides encrypted vs plain.
    void save_to_current_path() {
        if (has_txx_extension(filepath_)) {
            ensure_password_then_write();
        } else {
            password_.clear();
            write_file();
        }
    }

    void ensure_password_then_write() {
        if (!password_.empty()) { write_file(); return; }
        show_prompt("New Password", true, "", [this](bool ok, std::string p1){
            if (!ok) return;
            show_prompt("Confirm Password", true, "", [this, p1](bool ok2, std::string p2){
                if (!ok2) return;
                if (p1 != p2 || p1.empty()) {
                    show_alert("Password Error", "Passwords do not match.");
                    return;
                }
                password_ = p1; write_file();
            });
        });
    }

    void write_file() {
        std::string payload = join_with_style(buffer_.text(), settings_.newline_style);
        bool enc = has_txx_extension(filepath_);
        txx_status s;
        if (enc) {
            txx_buffer out; txx_buffer_init(&out);
            s = txx_encode_text(
                reinterpret_cast<const uint8_t *>(payload.data()), payload.size(),
                password_.c_str(), nullptr, &out);
            if (s != TXX_OK) {
                txx_buffer_secure_free(&out);
                show_alert("Error", std::string("Encode failed: ") + txx_status_string(s));
                return;
            }
            s = txx_write_file_atomic(filepath_.c_str(), out.data, out.len);
            txx_buffer_secure_free(&out);
        } else {
            s = txx_write_file_atomic(filepath_.c_str(),
                    reinterpret_cast<const uint8_t *>(payload.data()), payload.size());
        }
        if (s != TXX_OK) {
            show_alert("Error", std::string("Write failed: ") + txx_status_string(s));
            return;
        }
        dirty_ = false; config_save(settings_);
        set_status("Saved " + base_name(filepath_) + (enc ? "" : " (plain text)"));
    }

    void cmd_quit()   { confirm_discard([this]{ screen_->Exit(); }); }
    void cmd_cut()    { if (!buffer_.has_selection()) return;
                        clipboard_ = buffer_.selected_text(); clip::set_system(clipboard_);
                        edit([&]{ buffer_.delete_selection(); }); }
    void cmd_copy()   { if (!buffer_.has_selection()) return;
                        clipboard_ = buffer_.selected_text(); clip::set_system(clipboard_);
                        set_status("Copied"); }
    void cmd_paste()  { std::string sys;
                        if (clip::get_system(sys) && !sys.empty()) clipboard_ = sys;
                        if (clipboard_.empty()) return;
                        edit([&]{ buffer_.insert_text(clipboard_); }); }
    void cmd_find()   { show_prompt("Find", false, last_find_, [this](bool ok, std::string q){
                            if (!ok || q.empty()) return;
                            last_find_ = q; find_match(q, false, false); }); }
    void cmd_goto()   { show_prompt("Go to Line", false, "", [this](bool ok, std::string v){
                            if (!ok) return;
                            try { buffer_.goto_line(std::stoi(v)); } catch (...){} }); }
    void cmd_about() {
        TuiPluginDialogs dialogs("About etui", theme_);
        plugins::DialogRequest request{};
        request.kind = plugins::DialogKind::Message;
        request.prompt = about_text("etui - EncryptEd terminal editor",
            "FTXUI " ENCRYPTED_FTXUI_VERSION " - MIT (terminal UI)");
        dialogs.show(request);
    }

    void cmd_column_wrap() {
        show_prompt("Wrap Width", false, "80", [this](bool ok, std::string v){
            if (!ok) return;
            int w = 80; try { w = std::stoi(v); } catch (...){ return; }
            if (w < 1) return;
            transform([w](const std::string &t){ return tt_wrap_columns(t, w); });
        });
    }

    bool find_match(const std::string &query, bool previous, bool advance) {
        if (query.empty()) { set_status("Enter search text"); return false; }
        const std::string document = buffer_.text();
        std::size_t offset = buffer_.byte_position(buffer_.cursor());
        if (advance && buffer_.has_selection() && buffer_.selected_text() == query)
            offset = buffer_.byte_position(previous ? buffer_.selection_start() : buffer_.selection_end());
        else if (advance && !previous && offset < document.size())
            ++offset;
        std::size_t found = std::string::npos;
        bool wrapped = false;
        if (previous) {
            if (offset > 0) found = document.rfind(query, offset - 1);
            if (found == std::string::npos) { found = document.rfind(query); wrapped = true; }
        } else {
            found = document.find(query, offset);
            if (found == std::string::npos) { found = document.find(query); wrapped = true; }
        }
        if (found == std::string::npos) {
            buffer_.clear_selection();
            set_status("Not found: " + query);
            return false;
        }
        const Position first = buffer_.position_from_byte(found);
        buffer_.set_cursor(first);
        buffer_.set_selection(first, buffer_.position_from_byte(found + query.size()));
        set_status("Found: " + query + (wrapped ? " (wrapped)" : ""));
        return true;
    }

    void repeat_find(bool previous) {
        if (last_find_.empty()) { cmd_find(); return; }
        find_match(last_find_, previous, true);
    }

    void replace_match() {
        if (last_find_.empty()) { set_status("Enter search text"); return; }
        if (!buffer_.has_selection() || buffer_.selected_text() != last_find_) {
            find_match(last_find_, false, false);
            return; // First select a matching occurrence; never replace unrelated text.
        }
        if (last_find_ != last_replace_) {
            if (last_replace_.empty()) buffer_.delete_selection();
            else buffer_.insert_text(last_replace_);
            dirty_ = true;
        }
        // insert_text leaves the cursor after the replacement. Include that
        // position so adjacent occurrences are not skipped.
        find_match(last_find_, false, last_find_ == last_replace_);
    }

    void replace_all_matches() {
        if (last_find_.empty()) { set_status("Enter search text"); return; }
        const std::string document = buffer_.text();
        std::string result;
        std::size_t position = 0, count = 0;
        while (true) {
            const auto found = document.find(last_find_, position);
            if (found == std::string::npos) { result.append(document, position, std::string::npos); break; }
            result.append(document, position, found - position);
            result += last_replace_;
            position = found + last_find_.size();
            ++count;
        }
        if (!count) { set_status("Not found: " + last_find_); return; }
        const auto cursor = buffer_.byte_position(buffer_.cursor());
        if (buffer_.replace_text(result)) {
            dirty_ = true;
            buffer_.set_cursor(buffer_.position_from_byte(std::min(cursor, result.size())));
            buffer_.clear_selection();
            set_status("Replaced " + std::to_string(count) + " occurrences");
        } else set_status("No text changes");
    }

    void cmd_replace() {
        search_panel_open_ = true;
        auto next_action = [this] {
            find_match(last_find_, false, buffer_.has_selection() && buffer_.selected_text() == last_find_);
        };
        auto input_options = InputOption::Default();
        input_options.multiline = false;
        input_options.on_enter = next_action;
        auto query = Input(&last_find_, "Search text", input_options);
        auto replacement = Input(&last_replace_, "Replacement (empty deletes)", input_options);
        auto next = Button("Find Next", next_action, ButtonOption::Simple());
        auto previous = Button("Find Previous", [this]{ find_match(last_find_, true, true); }, ButtonOption::Simple());
        auto replace = Button("Replace", [this]{ replace_match(); }, ButtonOption::Simple());
        auto all = Button("Replace All", [this]{ replace_all_matches(); }, ButtonOption::Simple());
        auto close = Button("Close", [this]{ search_panel_open_ = false; }, ButtonOption::Simple());
        auto navigation = Container::Horizontal({next, previous});
        auto actions = Container::Horizontal({replace, all, close});
        auto container = Container::Vertical({query, replacement, navigation, actions});
        query->TakeFocus();
        search_panel_ = Renderer(container, [this, query, replacement, navigation, actions] {
            return chrome_style(window(text(" Find / Replace "), vbox({
                hbox({text("Find:         "), query->Render() | flex}),
                hbox({text("Replace with: "), replacement->Render() | flex}),
                separator(), navigation->Render(), actions->Render(), separator(),
                paragraph(status_msg_) | size(HEIGHT, LESS_THAN, 2),
                text("Case-sensitive literal search; searches wrap"),
                text("F3: next  F2: previous  Tab: controls  Esc: close") | dim
            })) | clear_under | size(WIDTH, LESS_THAN, std::min(76, Terminal::Size().dimx - 2)) | center);
        });
    }

    void confirm_discard(std::function<void()> action) {
        if (!dirty_) { action(); return; }
        show_confirm("Unsaved Changes", "Discard unsaved changes?",
            [action](bool ok){ if (ok) action(); });
    }

    // ---- Helpers -----------------------------------------------------------
    void set_status(const std::string &s) { status_msg_ = s; }

    void ensure_visible(int rows) {
        int cl = buffer_.cursor().line;
        if (cl < scroll_top_) scroll_top_ = cl;
        if (cl >= scroll_top_ + rows) scroll_top_ = cl - rows + 1;
        scroll_top_ = std::clamp(scroll_top_, 0, std::max(0, buffer_.line_count() - rows));
    }

    int view_rows() const { return std::max(1, Terminal::Size().dimy - 2); }

    // Columns available to text; the vertical scrollbar takes the last one.
    int text_cols() const {
        const bool bar = settings_.vertical_scrollbar && buffer_.line_count() > view_rows();
        return std::max(1, Terminal::Size().dimx - (bar ? 1 : 0));
    }

    static int cell_width(const std::string &line) {
        int cells = 0;
        std::size_t offset = 0;
        while (offset < line.size()) {
            const auto next = utf8_next(line, offset);
            cells += string_width(std::string_view(line.data() + offset, next - offset));
            offset = next;
        }
        return cells;
    }

    static int cell_at(const std::string &line, std::size_t byte) {
        int cells = 0;
        std::size_t offset = 0;
        const std::size_t target = std::min(byte, line.size());
        while (offset < target) {
            const auto next = utf8_next(line, offset);
            cells += string_width(std::string_view(line.data() + offset, next - offset));
            offset = next;
        }
        return cells;
    }

    // Byte offset of the first glyph starting at or after `cell`, so a slice
    // never begins in the middle of a double-width glyph.
    static std::size_t slice_from_cell(const std::string &line, int cell) {
        std::size_t offset = byte_at_cell(line, cell);
        while (offset < line.size() && cell_at(line, offset) < cell)
            offset = utf8_next(line, offset);
        return offset;
    }

    int max_scroll_left(int cols) const {
        int widest = 0;
        const int rows = view_rows();
        for (int i = 0; i < rows; ++i) {
            const int line = scroll_top_ + i;
            if (line >= buffer_.line_count()) break;
            widest = std::max(widest, cell_width(buffer_.line(line)));
        }
        return std::max(0, widest + 1 - cols);  // +1 keeps the end-of-line cursor reachable
    }

    void ensure_visible_h(int cols) {
        const std::string &ln = buffer_.line(buffer_.cursor().line);
        const int cc = cell_at(ln, byte_at(ln, buffer_.cursor().col));
        if (cc < scroll_left_) scroll_left_ = cc;
        if (cc >= scroll_left_ + cols) scroll_left_ = cc - cols + 1;
        scroll_left_ = std::clamp(scroll_left_, 0, max_scroll_left(cols));
    }

    bool code_state(int idx) const {
        bool inside = false;
        for (int i = 0; i < idx; ++i)
            if (md_is_fence(buffer_.line(i))) inside = !inside;
        if (md_is_fence(buffer_.line(idx))) return false;
        return inside;
    }

    static std::size_t byte_at(const std::string &line, int col) {
        const char *p = line.c_str(), *end = p + line.size();
        int c = 0;
        while (p < end && c < col) {
            int len = 0; utf8_decode_next(p, end, &len);
            if (len <= 0) len = 1;
            p += len; ++c;
        }
        return static_cast<std::size_t>(p - line.c_str());
    }

    static int col_at(const std::string &line, std::size_t byte) {
        const char *p = line.c_str(), *end = p + line.size();
        const char *target = p + std::min(byte, line.size());
        int c = 0;
        while (p < target) {
            int len = 0; utf8_decode_next(p, end, &len);
            if (len <= 0) len = 1;
            p += len; ++c;
        }
        return c;
    }

    static std::string base_name(const std::string &path) {
        auto pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    // ---- State -------------------------------------------------------------
    App *screen_;
    Theme theme_;
    EditBuffer         buffer_;
    bool plugin_running_ = false;
    plugins::Catalog plugin_catalog_;
    std::filesystem::path plugin_root_ = plugins::user_directory();
    AppSettings        settings_;
    std::string        filepath_;
    std::string        password_;
    std::string        pending_open_;
    std::string        clipboard_;
    std::string        last_find_;
    std::string        last_replace_;
    bool search_panel_open_ = false;
    Component search_panel_;
    std::string        status_msg_;
    bool               dirty_      = false;
    bool               mouse_down_ = false;
    int                scroll_top_ = 0;
    int                scroll_left_ = 0;
    SynLang            syn_lang_   = SynLang::None;

    enum class DialogKind { None, Prompt, Confirm, Alert };
    DialogKind  dialog_kind_  = DialogKind::None;
    int         dialog_btn_   = 0;  // focused button (0=primary, 1=secondary)
    std::string dialog_title_;
    std::string dialog_body_;
    bool        dialog_mask_  = false;
    std::string dialog_input_;
    std::size_t dialog_cursor_ = 0;
    mutable Box dialog_input_box_{};
    std::function<void(bool, std::string)> dialog_cb_;
    mutable Box dialog_btn0_box_{};
    mutable Box dialog_btn1_box_{};

    // ---- File picker state -------------------------------------------------
    enum class PickerMode { Open, Save };
    enum { FOCUS_LIST = 0, FOCUS_NAME = 1, FOCUS_ENC = 2, FOCUS_PARENT = 3, FOCUS_ACTION = 4, FOCUS_CANCEL = 5 };  // save-mode focus
    struct PickerEntry { std::string name; bool is_dir; };  // ".." has is_dir=true
    bool                     picker_open_       = false;
    PickerMode               picker_mode_       = PickerMode::Open;
    std::filesystem::path    picker_dir_;
    std::vector<PickerEntry> picker_entries_;
    int                      picker_sel_        = 0;
    int                      picker_scroll_     = 0;
    std::string              save_name_;
    int                      save_focus_        = FOCUS_NAME;
    std::size_t              save_cursor_       = 0;  // byte offset into save_name_
    int                      picker_last_click_ = -1;  // entry index of last single click
    mutable std::vector<Box> picker_row_boxes_;         // one per visible row (reflect slots)
    mutable Box              picker_name_box_{};
    mutable Box              picker_enc_box_{};
    mutable Box              picker_parent_box_{};
    mutable Box              picker_action_box_{};
    mutable Box              picker_cancel_box_{};
};

int main(int argc, char **argv) {
    std::string path = (argc > 1) ? argv[1] : "";
    std::string piped;

#ifndef _WIN32
    // `etui -` or `something | etui` opens standard input as the document;
    // FTXUI then takes the keyboard from the terminal (/dev/tty). A path
    // argument wins, so scripts can redirect stdin without changing behaviour.
    if (path == "-") path.clear();
    if (path.empty() && !isatty(STDIN_FILENO)) {
        char buffer[65536];
        ssize_t count = 0;
        while ((count = read(STDIN_FILENO, buffer, sizeof buffer)) > 0)
            piped.append(buffer, static_cast<std::size_t>(count));
    }

    struct termios original{};
    bool restore = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original) == 0;
    if (restore) {
        struct termios t = original;
        t.c_iflag &= ~(IXON | IXOFF);
        // Ctrl+C and Ctrl+Z are editor commands, not terminal signals.
        t.c_lflag &= ~ISIG;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
#endif

    {
        auto screen = App::Fullscreen();
        // Ctrl+C (copy) and Ctrl+Z (undo) are editor commands; without these
        // FTXUI also exits or suspends the process when they are pressed.
        screen.ForceHandleCtrlC(false);
        screen.ForceHandleCtrlZ(false);
        auto app    = std::make_shared<EditorApp>(&screen, path, std::move(piped));
        screen.Loop(app);
    }

#ifndef _WIN32
    if (restore) tcsetattr(STDIN_FILENO, TCSANOW, &original);
#endif
    return 0;
}
