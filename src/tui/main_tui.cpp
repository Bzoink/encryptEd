// etui — text-mode (terminal) front-end for encryptEd.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

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

#include "gui/text_transform.h"
#include "tui/clipboard.h"
#include "tui/config.h"
#include "tui/edit_buffer.h"
#include "tui/md_parse.h"
#include "tui/syn_parse.h"

using namespace ftxui;

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
    explicit EditorApp(ScreenInteractive *screen, std::string open_path)
        : screen_(screen), pending_open_(std::move(open_path)) {
        config_load(settings_);
        buffer_.set_text("");
        build_menus();
        set_status("New document");
        if (!pending_open_.empty())
            open_path_flow(pending_open_);
    }

    bool Focusable() const override { return true; }

    Element Render() override {
        Dimensions dim = Terminal::Size();
        int rows = std::max(1, dim.dimy - 2);
        ensure_visible(rows);

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
            doc.push_back(line < total ? render_line(line, syn_st) : text(""));
        }

        Element editor = vbox(std::move(doc)) | flex;
        Element top    = render_menu_bar();
        Element bottom = render_status();
        Element base   = vbox({top, editor, bottom});

        if (menu_open_ >= 0)
            base = dbox({base, render_menu_overlay()});
        if (picker_open_)
            base = dbox({base, render_picker_overlay()});
        if (dialog_kind_ != DialogKind::None)
            base = dbox({base, render_dialog()});

        return base;
    }

    bool OnEvent(Event event) override {
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
        ed.push_back({"Undo",       "Ctrl+Z", [this]{ edit([&]{ buffer_.undo(); }); }});
        ed.push_back({"Redo",       "Ctrl+Y", [this]{ edit([&]{ buffer_.redo(); }); }});
        ed.push_back({});
        ed.push_back({"Cut",        "Ctrl+X", [this]{ cmd_cut(); }});
        ed.push_back({"Copy",       "Ctrl+C", [this]{ cmd_copy(); }});
        ed.push_back({"Paste",      "Ctrl+V", [this]{ cmd_paste(); }});
        ed.push_back({"Select All", "Ctrl+A", [this]{ buffer_.select_all(); }});
        ed.push_back({});
        ed.push_back({"Find...",       "Ctrl+F", [this]{ cmd_find(); }});
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

        // ---- Settings ------------------------------------------------------
        std::vector<MenuEntry> se;
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
        menus_.push_back({"Settings", std::move(se)});

        // ---- Help ----------------------------------------------------------
        std::vector<MenuEntry> he;
        he.push_back({"About", "", [this]{ cmd_about(); }});
        menus_.push_back({"Help", std::move(he)});
    }

    // ---- Rendering ---------------------------------------------------------
    Color style_color(MdStyle s) const {
        switch (s) {
            case MdStyle::Heading:    return Color::CyanLight;
            case MdStyle::CodeBlock:
            case MdStyle::InlineCode: return Color::GreenLight;
            case MdStyle::Strong:     return Color::YellowLight;
            case MdStyle::Emphasis:   return Color::Yellow;
            case MdStyle::Quote:      return Color::GrayLight;
            case MdStyle::ListMarker: return Color::RedLight;
            case MdStyle::Link:       return Color::BlueLight;
            case MdStyle::Rule:       return Color::GrayDark;
            default:                  return Color::White;
        }
    }

    Color syn_color(SynStyle s) const {
        switch (s) {
            case SynStyle::Keyword: return Color::CyanLight;
            case SynStyle::String:  return Color::YellowLight;
            case SynStyle::Comment: return Color::GrayDark;
            case SynStyle::Number:  return Color::RedLight;
            case SynStyle::Preproc: return Color::GreenLight;
            default:                return Color::White;
        }
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
            if (i == menu_open_) t = t | inverted;
            t = t | reflect(menus_[i].title_box);
            parts.push_back(t);
        }
        parts.push_back(filler());
        return hbox(std::move(parts)) | inverted;
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
                auto state = EntryState{e.label, e.checked(), false, false};
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
            if (i == sel) row = row | inverted;
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
        Element dd = window(text(""), vbox(std::move(rows))) | clear_under;

        Element overlay = place_at(ddx, 1, dd);

        // Submenu: open when an item with children is highlighted
        bool show_sub = menu_item_ >= 0 && menu_item_ < (int)items.size() &&
                        !items[menu_item_].sub.empty();
        if (show_sub) {
            const auto &sub_items = items[menu_item_].sub;
            int siw  = items_inner_width(sub_items);
            int sub_sel = menu_in_sub_ ? menu_sub_ : -1;
            auto sub_rows = build_rows(sub_items, sub_sel, siw);
            Element sub_dd = window(text(""), vbox(std::move(sub_rows))) | clear_under;
            // x: right edge of main dropdown; y: aligned with triggering item row
            int sub_x = ddx + ddw;
            int sub_y = 1 + menu_item_;   // 1 = menu bar; menu_item_ = rows from top border
            overlay = dbox({overlay, place_at(sub_x, sub_y, sub_dd)});
        }

        return overlay;
    }

    Element render_line(int line_idx, SynState &syn_st) {
        const std::string &ln = buffer_.line(line_idx);
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
            return render_selection_line(ln, from, to);
        }

        // Locate cursor within this line so we can split a styled segment at it.
        bool show_cursor = (line_idx == buffer_.cursor().line &&
                            menu_open_ < 0 && dialog_kind_ == DialogKind::None &&
                            !picker_open_);
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
                if (a >= b) return;
                Element e = text(ln.substr(a, b - a));
                if (seg.has_col) e = e | color(seg.col);
                if (is_cur) e = e | inverted | focusCursorBarBlinking;
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
        // Cursor past end of line: trailing inverted space.
        if (show_cursor && cur_clen == 0)
            out.push_back(text(" ") | inverted | focusCursorBarBlinking);

        if (out.empty()) out.push_back(text(""));
        return hbox(std::move(out));
    }

    Element render_selection_line(const std::string &ln, int from, int to) {
        std::size_t b_from = byte_at(ln, from);
        std::size_t b_to   = byte_at(ln, to);
        Elements spans;
        if (b_from > 0) spans.push_back(text(ln.substr(0, b_from)));
        std::string mid = ln.substr(b_from, b_to - b_from);
        if (mid.empty() && from == to) mid = " ";
        spans.push_back(text(mid) | inverted);
        if (b_to < ln.size()) spans.push_back(text(ln.substr(b_to)));
        return hbox(std::move(spans));
    }

    Element render_status() {
        Position c = buffer_.cursor();
        std::string count =
            settings_.status_count == AppSettings::STATUS_COUNT_BYTES
                ? std::to_string(buffer_.byte_count()) + " bytes"
                : std::to_string(buffer_.char_count()) + " chars";
        std::string right = count + "  " + newline_label(settings_.newline_style) +
                            "  Ln " + std::to_string(c.line + 1) +
                            ", Col " + std::to_string(c.col + 1) +
                            "  " + (buffer_.overwrite() ? "OVR" : "INS") + " ";
        return hbox({text(" " + status_msg_), filler(), text(right)}) | inverted;
    }

    Element render_dialog() {
        Dimensions dim = Terminal::Size();
        int dw      = std::min(54, std::max(30, dim.dimx - 4));
        int inner_w = dw - 2;

        // Helper: render a single button, reflected for mouse hit-testing.
        auto make_btn = [](const std::string &label, bool active, Box &box) -> Element {
            Element e = text("[ " + label + " ]");
            if (active) e = e | inverted;
            return e | reflect(box);
        };

        Elements rows;
        rows.push_back(text(""));  // top padding

        if (!dialog_body_.empty())
            rows.push_back(text("  " + dialog_body_) | size(WIDTH, EQUAL, inner_w));

        if (dialog_kind_ == DialogKind::Prompt) {
            if (!dialog_body_.empty()) rows.push_back(text(""));
            std::string shown = dialog_mask_
                ? std::string(dialog_input_.size(), '*') : dialog_input_;
            rows.push_back(
                hbox({text("  > "), text(shown),
                      text(" ") | inverted | focusCursorBarBlinking, filler()})
                | size(WIDTH, EQUAL, inner_w));
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
            Element b0 = make_btn("OK",     false, dialog_btn0_box_);
            Element b1 = make_btn("Cancel", false, dialog_btn1_box_);
            rows.push_back(hbox({filler(), std::move(b0), text("    "),
                                           std::move(b1), filler()}));
        }

        rows.push_back(text(""));  // bottom padding

        Element dlg = window(text(" " + dialog_title_ + " "), vbox(std::move(rows)))
                    | clear_under
                    | size(WIDTH, EQUAL, dw);

        int dh = 7 + (!dialog_body_.empty() && dialog_kind_ == DialogKind::Prompt ? 2 : 0);
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
        return std::min(14, std::max(3, dim.dimy - 8 - extra));
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
    void picker_load(const std::filesystem::path &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path abs = fs::weakly_canonical(dir, ec);
        if (ec || abs.empty()) { ec.clear(); abs = fs::absolute(dir, ec); }
        if (abs.empty()) abs = dir;

        std::vector<PickerEntry> entries;
        ec.clear();
        fs::directory_iterator it(abs, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            show_alert("Cannot Open", "Cannot read directory: " + abs.string());
            return;
        }
        for (fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;  // hide dotfiles
            std::error_code dec;
            entries.push_back({name, it->is_directory(dec)});
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

        picker_row_boxes_.resize(view_h);
        bool list_focused = !save || save_focus_ == FOCUS_LIST;
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
                if (e.is_dir) row = row | color(Color::CyanLight);
                if (idx == picker_sel_ && list_focused) row = row | inverted;
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
                        text(save_name_.substr(cur, csz)) | inverted | focusCursorBarBlinking);
                    if (cur + csz < save_name_.size())
                        name_cells.push_back(text(save_name_.substr(cur + csz)));
                } else {
                    name_cells.push_back(text(" ") | inverted | focusCursorBarBlinking);
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
                                      false, save_focus_ == FOCUS_ENC};
            Element cb = hbox({text(" "),
                              CheckboxOption::Simple().transform(cbstate), filler()})
                       | size(WIDTH, EQUAL, inner_w);
            if (save_focus_ == FOCUS_ENC) cb = cb | inverted;
            rows.push_back(cb | reflect(picker_enc_box_));

            rows.push_back(separator());
            rows.push_back(text(" Tab focus  Enter save  Esc cancel")
                           | size(WIDTH, EQUAL, inner_w));
        } else {
            rows.push_back(text(" \xE2\x86\x91\xE2\x86\x93 move  Enter open  "
                                "\xE2\x86\x90 parent  Esc cancel")
                           | size(WIDTH, EQUAL, inner_w));
        }

        Element dlg = window(text(save ? " Save File " : " Open File "),
                             vbox(std::move(rows)))
                    | clear_under | size(WIDTH, EQUAL, dw);

        int dh = view_h + 6 + (save ? 3 : 0);  // +name +checkbox +separator
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

                if (save && hit(picker_name_box_)) {
                    save_focus_        = FOCUS_NAME;
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

                    if (save) save_focus_ = FOCUS_LIST;

                    if (picker_sel_ == idx && picker_last_click_ == idx) {
                        picker_last_click_ = -1;
                        const PickerEntry entry = picker_entries_[idx];
                        if (entry.name == "..") {
                            picker_load(picker_dir_.parent_path());
                        } else if (entry.is_dir) {
                            picker_load(picker_dir_ / entry.name);
                        } else if (save) {
                            save_name_   = entry.name;
                            save_cursor_ = save_name_.size();
                            save_focus_  = FOCUS_NAME;
                        } else {
                            std::string full = (picker_dir_ / entry.name).string();
                            if (open_path_flow(full)) picker_open_ = false;
                        }
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

        // Save mode — filename input has focus.
        if (save && save_focus_ == FOCUS_NAME) {
            if (e == Event::Return)     { commit_save();                              return true; }
            if (e == Event::Tab)        { save_focus_ = FOCUS_ENC;                   return true; }
            if (e == Event::TabReverse) { save_focus_ = FOCUS_LIST;                  return true; }
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
            if (e == Event::Tab)        { save_focus_ = FOCUS_LIST; return true; }
            if (e == Event::TabReverse) { save_focus_ = FOCUS_NAME; return true; }
            if (e == Event::ArrowUp)    { save_focus_ = FOCUS_NAME; return true; }
            if (e == Event::ArrowDown)  { save_focus_ = FOCUS_LIST; return true; }
            return true;
        }

        // List focus — open mode always, save mode when the list is focused.
        if (save && e == Event::Tab)        { save_focus_ = FOCUS_NAME; return true; }
        if (save && e == Event::TabReverse) { save_focus_ = FOCUS_ENC;  return true; }

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

        if (e == Event::Return) {
            if (!total) return true;
            const PickerEntry entry = picker_entries_[picker_sel_];
            if (entry.name == "..") {
                picker_load(picker_dir_.parent_path());
            } else if (entry.is_dir) {
                picker_load(picker_dir_ / entry.name);
            } else if (save) {
                save_name_   = entry.name;
                save_cursor_ = save_name_.size();
                save_focus_  = FOCUS_NAME;
            } else {
                std::string full = (picker_dir_ / entry.name).string();
                if (open_path_flow(full)) picker_open_ = false;
            }
            return true;
        }

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
        if (m.button == Mouse::Left) {
            // Open menu when clicking row 0 (menu bar).
            if (m.y == 0) {
                int top = top_menu_at(m.x);
                if (top >= 0) { open_top(top); return true; }
                return true;
            }
            // Editor area: row 1 = first editor line.
            int line = scroll_top_ + m.y - 1;
            int col  = m.x;
            if (line < 0) line = 0;
            if (col  < 0) col  = 0;
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
        dialog_btn_   = 0;
        dialog_title_ = std::move(title);
        dialog_body_  = {};
        dialog_mask_  = mask;
        dialog_input_ = std::move(initial);
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
        dialog_kind_  = DialogKind::Alert;
        dialog_btn_   = 0;
        dialog_title_ = std::move(title);
        dialog_body_  = std::move(body);
        dialog_mask_  = false;
        dialog_input_ = {};
        dialog_cb_    = nullptr;
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

        // Prompt — Enter submits, Esc cancels regardless of button focus.
        if (e == Event::Escape)   { close_dialog(false); return true; }
        if (e == Event::Return)   { close_dialog(true);  return true; }
        if (e == Event::Backspace) {
            if (!dialog_input_.empty()) {
                std::size_t n = dialog_input_.size();
                do { --n; } while (n > 0 &&
                    (static_cast<unsigned char>(dialog_input_[n]) & 0xC0) == 0x80);
                dialog_input_.erase(n);
            }
            return true;
        }
        if (e.is_character()) { dialog_input_ += e.character(); return true; }
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
            dirty_ = false; syn_lang_ = SynLang::None; set_status("New document");
        });
    }

    void cmd_open() {
        confirm_discard([this]{
            std::filesystem::path start = filepath_.empty()
                ? std::filesystem::current_path()
                : std::filesystem::path(filepath_).parent_path();
            picker_mode_ = PickerMode::Open;
            picker_load(start);
            picker_open_ = true;
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
            filepath_ = path; password_.clear(); dirty_ = false; scroll_top_ = 0;
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
            filepath_ = path; password_ = pw; dirty_ = false; scroll_top_ = 0;
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
        picker_load(start);
        picker_open_ = true;
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
                            last_find_ = q; find_next(q); }); }
    void cmd_goto()   { show_prompt("Go to Line", false, "", [this](bool ok, std::string v){
                            if (!ok) return;
                            try { buffer_.goto_line(std::stoi(v)); } catch (...){} }); }
    void cmd_about()  { show_alert("About",
                            "etui \xe2\x80\x94 encryptEd terminal editor. "
                            "F9 or Alt+letter to open a menu."); }

    void cmd_column_wrap() {
        show_prompt("Wrap Width", false, "80", [this](bool ok, std::string v){
            if (!ok) return;
            int w = 80; try { w = std::stoi(v); } catch (...){ return; }
            if (w < 1) return;
            transform([w](const std::string &t){ return tt_wrap_columns(t, w); });
        });
    }

    void find_next(const std::string &q) {
        std::string hay = buffer_.text();
        Position c = buffer_.cursor();
        std::size_t off = 0;
        for (int l = 0; l < c.line; ++l) off += buffer_.line(l).size() + 1;
        off += byte_at(buffer_.line(c.line), c.col);
        std::size_t found = hay.find(q, off + 1);
        if (found == std::string::npos) found = hay.find(q);
        if (found == std::string::npos) { set_status("Not found: " + q); return; }
        int line = 0; std::size_t run = 0;
        while (line < buffer_.line_count() &&
               run + buffer_.line(line).size() + 1 <= found) {
            run += buffer_.line(line).size() + 1; ++line;
        }
        buffer_.set_cursor(Position{line, col_at(buffer_.line(line), found - run)});
        buffer_.clear_selection();
        set_status("Found: " + q);
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
        if (scroll_top_ < 0) scroll_top_ = 0;
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
    ScreenInteractive *screen_;
    EditBuffer         buffer_;
    AppSettings        settings_;
    std::string        filepath_;
    std::string        password_;
    std::string        pending_open_;
    std::string        clipboard_;
    std::string        last_find_;
    std::string        status_msg_;
    bool               dirty_      = false;
    bool               mouse_down_ = false;
    int                scroll_top_ = 0;
    SynLang            syn_lang_   = SynLang::None;

    enum class DialogKind { None, Prompt, Confirm, Alert };
    DialogKind  dialog_kind_  = DialogKind::None;
    int         dialog_btn_   = 0;  // focused button (0=primary, 1=secondary)
    std::string dialog_title_;
    std::string dialog_body_;
    bool        dialog_mask_  = false;
    std::string dialog_input_;
    std::function<void(bool, std::string)> dialog_cb_;
    mutable Box dialog_btn0_box_{};
    mutable Box dialog_btn1_box_{};

    // ---- File picker state -------------------------------------------------
    enum class PickerMode { Open, Save };
    enum { FOCUS_LIST = 0, FOCUS_NAME = 1, FOCUS_ENC = 2 };  // save-mode focus
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
};

int main(int argc, char **argv) {
    std::string path = (argc > 1) ? argv[1] : "";

#ifndef _WIN32
    struct termios original{};
    bool restore = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original) == 0;
    if (restore) {
        struct termios t = original;
        t.c_iflag &= ~(IXON | IXOFF);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
#endif

    {
        auto screen = ScreenInteractive::Fullscreen();
        auto app    = std::make_shared<EditorApp>(&screen, path);
        screen.Loop(app);
    }

#ifndef _WIN32
    if (restore) tcsetattr(STDIN_FILENO, TCSANOW, &original);
#endif
    return 0;
}
