#include "gui/highlight.h"
#include "gui/secure_text.h"
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// Style byte definitions
// The char value minus 'A' is the index into Highlighter::table_[].
// ---------------------------------------------------------------------------
static constexpr char S_PLAIN   = 'A';  // plain text
static constexpr char S_HEADING = 'B';  // # Heading
static constexpr char S_STRONG  = 'C';  // **bold**  __bold__
static constexpr char S_EM      = 'D';  // *italic*  _italic_
static constexpr char S_CODE    = 'E';  // `inline code`
static constexpr char S_FENCE   = 'F';  // ~~~ fenced block
static constexpr char S_LINK    = 'G';  // [link](url)  ![img](url)
static constexpr char S_QUOTE   = 'H';  // > blockquote
static constexpr char S_LIST    = 'I';  // - item  1. item  (marker only)
static constexpr char S_SPECIAL = 'J';  // ~~strikeout~~  ---HR---

// ---------------------------------------------------------------------------
// Inline markup scanner
// Operates on a single line (no newline bytes).  out[] is pre-filled with
// `plain` and we write over positions that have markup.
// Priority (highest first): code spans, bold, italic, strikethrough, links.
// ---------------------------------------------------------------------------
void Highlighter::scan_inline(const char *text, int len, char *out, char plain)
{
    // ---- Phase 1: code spans (`...` and ``...``) ---------------------------
    {
        int i = 0;
        while (i < len) {
            if (out[i] != plain || text[i] != '`') { ++i; continue; }
            int ts = i, nt = 0;
            while (i < len && text[i] == '`') { ++nt; ++i; }
            // Find matching closing run of exactly nt backticks
            int j = i;
            bool found = false;
            while (j < len && !found) {
                if (text[j] != '`') { ++j; continue; }
                int nc = 0;
                while (j < len && text[j] == '`') { ++nc; ++j; }
                if (nc == nt) {
                    for (int k = ts; k < j; ++k) out[k] = S_CODE;
                    found = true;
                }
            }
            // No close found: leave opening ticks as plain, cursor already past them
        }
    }

    // ---- Phase 2: bold ** and __ -------------------------------------------
    for (int m = 0; m < 2; ++m) {
        const char c = (m == 0) ? '*' : '_';
        int i = 0;
        while (i <= len - 4) {
            if (out[i] != plain || text[i] != c || text[i + 1] != c) { ++i; continue; }
            // Avoid matching inside a *** sequence
            if (i > 0 && text[i - 1] == c) { ++i; continue; }
            const int os = i;
            i += 2;
            // Find closing **
            for (int j = i; j <= len - 2; ++j) {
                if (out[j] != plain || text[j] != c || text[j + 1] != c) continue;
                // Not part of ***
                if (j + 2 < len && text[j + 2] == c) continue;
                for (int k = os; k < j + 2; ++k) out[k] = S_STRONG;
                i = j + 2;
                break;
            }
        }
    }

    // ---- Phase 3: italic * and _ -------------------------------------------
    for (int m = 0; m < 2; ++m) {
        const char c = (m == 0) ? '*' : '_';
        int i = 0;
        while (i < len) {
            if (out[i] != plain || text[i] != c) { ++i; continue; }
            // Skip if it's part of ** (already styled or adjacent double)
            if (i + 1 < len && text[i + 1] == c) { ++i; continue; }
            if (i > 0  && text[i - 1] == c) { ++i; continue; }
            const int os = i++;
            for (int j = i; j < len; ++j) {
                if (out[j] != plain || text[j] != c) continue;
                if (j + 1 < len && text[j + 1] == c) continue;
                if (j > 0  && text[j - 1] == c) continue;
                for (int k = os; k <= j; ++k) out[k] = S_EM;
                i = j + 1;
                break;
            }
        }
    }

    // ---- Phase 4: strikethrough ~~ -----------------------------------------
    {
        int i = 0;
        while (i <= len - 4) {
            if (out[i] != plain || text[i] != '~' || text[i + 1] != '~') { ++i; continue; }
            const int os = i;
            i += 2;
            for (int j = i; j <= len - 2; ++j) {
                if (out[j] != plain || text[j] != '~' || text[j + 1] != '~') continue;
                for (int k = os; k < j + 2; ++k) out[k] = S_SPECIAL;
                i = j + 2;
                break;
            }
        }
    }

    // ---- Phase 5: links [text](url) and images ![text](url) ----------------
    {
        int i = 0;
        while (i < len) {
            if (out[i] != plain) { ++i; continue; }
            const int ls = i;
            bool is_img = (text[i] == '!' && i + 1 < len && text[i + 1] == '[');
            if (!is_img && text[i] != '[') { ++i; continue; }
            if (is_img) ++i;
            ++i; // skip [
            // Find matching ]
            int depth = 1;
            while (i < len && depth > 0) {
                if (out[i] == plain) {
                    if (text[i] == '[')      ++depth;
                    else if (text[i] == ']') --depth;
                }
                ++i;
            }
            // Expect ( for inline link
            if (i < len && text[i] == '(') {
                ++i;
                int pd = 1;
                while (i < len && pd > 0) {
                    if (text[i] == '(')      ++pd;
                    else if (text[i] == ')') --pd;
                    ++i;
                }
                for (int k = ls; k < i; ++k) if (out[k] == plain) out[k] = S_LINK;
            }
            // Reference-style link: [text][ref]
            else if (i < len && text[i] == '[') {
                ++i;
                while (i < len && text[i] != ']') ++i;
                if (i < len) ++i;
                for (int k = ls; k < i; ++k) if (out[k] == plain) out[k] = S_LINK;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Full-document scanner
// ---------------------------------------------------------------------------
void Highlighter::scan(const char *text, int len, char *out)
{
    std::memset(out, S_PLAIN, static_cast<std::size_t>(len));

    bool in_fence   = false;
    char fence_char = '~';
    bool in_table   = false;

    // ---- Helpers for GFM table detection ----------------------------------
    auto line_has_pipe = [&](int s, int e) {
        for (int k = s; k < e; ++k) if (text[k] == '|') return true;
        return false;
    };
    // A separator line has at least one '|' and one '-', and contains only
    // characters from the set { '|', '-', ':', ' ', '\t' }.
    auto is_table_separator = [&](int s, int e) {
        bool seen_pipe = false, seen_dash = false;
        for (int k = s; k < e; ++k) {
            const char c = text[k];
            if (c == '|') seen_pipe = true;
            else if (c == '-') seen_dash = true;
            else if (c != ':' && c != ' ' && c != '\t') return false;
        }
        return seen_pipe && seen_dash;
    };
    // Style one body / header row: pipes get S_LIST, cells are either
    // scan_inline()'d (body) or filled with a fixed style (header bold).
    auto style_table_row = [&](char *o, const char *l, int n, char cell_style) {
        int k = 0;
        while (k < n) {
            if (l[k] == '|') { o[k] = S_LIST; ++k; continue; }
            int seg_start = k;
            while (k < n && l[k] != '|') ++k;
            // Trim leading whitespace from the cell so styling starts at content
            int ts = seg_start;
            while (ts < k && (l[ts] == ' ' || l[ts] == '\t')) ++ts;
            if (cell_style == S_PLAIN) {
                scan_inline(l + ts, k - ts, o + ts, S_PLAIN);
            } else {
                for (int m = ts; m < k; ++m) o[m] = cell_style;
            }
        }
    };

    int i = 0;
    while (i < len) {
        // Locate line bounds
        const int line_start = i;
        int line_end = i;
        while (line_end < len && text[line_end] != '\n' && text[line_end] != '\r')
            ++line_end;
        int next_i = line_end;
        if (next_i < len && text[next_i] == '\r') ++next_i;
        if (next_i < len && text[next_i] == '\n') ++next_i;

        const int llen    = line_end - line_start;
        const char *line  = text + line_start;
        char       *lout  = out  + line_start;

        // ---- Inside fenced code block ---------------------------------------
        if (in_fence) {
            // Check for closing fence: 0-3 leading spaces, then 3+ fence_char, rest blank
            int j = 0;
            while (j < llen && line[j] == ' ' && j < 3) ++j;
            int fc = 0;
            while (j + fc < llen && line[j + fc] == fence_char) ++fc;
            bool rest_blank = true;
            for (int k = j + fc; k < llen; ++k)
                if (line[k] != ' ' && line[k] != '\t') { rest_blank = false; break; }
            std::memset(lout, S_FENCE, llen);
            if (fc >= 3 && rest_blank)
                in_fence = false;
            // Style the newline bytes to match
            for (int k = line_end; k < next_i; ++k)
                out[k] = in_fence ? S_FENCE : S_PLAIN;
            i = next_i;
            continue;
        }

        // ---- Inside a GFM table ---------------------------------------------
        if (in_table) {
            if (llen == 0 || !line_has_pipe(line_start, line_end)) {
                in_table = false;
                // fall through to normal handling for this line
            } else if (is_table_separator(line_start, line_end)) {
                std::memset(lout, S_SPECIAL, llen);
                i = next_i;
                continue;
            } else {
                style_table_row(lout, line, llen, S_PLAIN);
                i = next_i;
                continue;
            }
        }

        // ---- Blank line -----------------------------------------------------
        if (llen == 0) { i = next_i; continue; }

        // ---- GFM table header (lookahead: next line is a separator) --------
        if (line_has_pipe(line_start, line_end)) {
            int n_start = next_i;
            int n_end   = n_start;
            while (n_end < len && text[n_end] != '\n' && text[n_end] != '\r')
                ++n_end;
            if (n_end > n_start && is_table_separator(n_start, n_end)) {
                style_table_row(lout, line, llen, S_STRONG);
                in_table = true;
                i = next_i;
                continue;
            }
        }

        // Skip up to 3 leading spaces (CommonMark: not an indented code block)
        int indent = 0;
        while (indent < llen && indent < 3 && line[indent] == ' ') ++indent;

        // ---- ATX heading: # Foo, ## Foo, ..., ###### Foo --------------------
        if (line[indent] == '#') {
            int hn = 0;
            while (indent + hn < llen && line[indent + hn] == '#') ++hn;
            if (hn <= 6 &&
                (indent + hn >= llen ||
                 line[indent + hn] == ' ' || line[indent + hn] == '\t')) {
                std::memset(lout, S_HEADING, llen);
                i = next_i;
                continue;
            }
        }

        // ---- Fenced code block opening: ~~~ or ``` -------------------------
        if (llen - indent >= 3) {
            const char fc = line[indent];
            if (fc == '~' || fc == '`') {
                int fc_count = 0;
                while (indent + fc_count < llen && line[indent + fc_count] == fc)
                    ++fc_count;
                if (fc_count >= 3) {
                    std::memset(lout, S_FENCE, llen);
                    in_fence   = true;
                    fence_char = fc;
                    i = next_i;
                    continue;
                }
            }
        }

        // ---- Blockquote: > .------------------------------------------------
        if (line[indent] == '>') {
            lout[indent] = S_QUOTE;
            int cs = indent + 1;
            if (cs < llen && line[cs] == ' ') ++cs;
            scan_inline(line + cs, llen - cs, lout + cs, S_PLAIN);
            i = next_i;
            continue;
        }

        // ---- Horizontal rule: 3+ of -, *, _ with optional spaces ------------
        {
            char hr = '\0';
            int  nc = 0;
            bool ok = true;
            for (int j = indent; j < llen; ++j) {
                if (line[j] == '-' || line[j] == '*' || line[j] == '_') {
                    if (hr == '\0') hr = line[j];
                    if (line[j] == hr) { ++nc; }
                    else { ok = false; break; }
                } else if (line[j] != ' ' && line[j] != '\t') {
                    ok = false; break;
                }
            }
            if (ok && nc >= 3 && hr != '\0') {
                std::memset(lout, S_SPECIAL, llen);
                i = next_i;
                continue;
            }
        }

        // ---- Unordered list: -, +, * followed by space ----------------------
        if (indent < llen &&
            (line[indent] == '-' || line[indent] == '+' || line[indent] == '*') &&
            indent + 1 < llen &&
            (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
            for (int j = 0; j <= indent; ++j) lout[j] = S_LIST;
            const int cs = indent + 2;
            scan_inline(line + cs, llen - cs, lout + cs, S_PLAIN);
            i = next_i;
            continue;
        }

        // ---- Ordered list: 1. or 1) followed by space -----------------------
        if (indent < llen && line[indent] >= '1' && line[indent] <= '9') {
            int j = indent;
            while (j < llen && line[j] >= '0' && line[j] <= '9') ++j;
            if (j < llen && (line[j] == '.' || line[j] == ')') &&
                j + 1 < llen && (line[j + 1] == ' ' || line[j + 1] == '\t')) {
                for (int k = 0; k <= j; ++k) lout[k] = S_LIST;
                const int cs = j + 2;
                scan_inline(line + cs, llen - cs, lout + cs, S_PLAIN);
                i = next_i;
                continue;
            }
        }

        // ---- Plain line: scan inline markup only ----------------------------
        scan_inline(line, llen, lout, S_PLAIN);
        i = next_i;
    }
}

// ---------------------------------------------------------------------------
// Highlighter — public methods
// ---------------------------------------------------------------------------
Highlighter::~Highlighter()
{
    detach();
}

void Highlighter::attach(Fl_Text_Editor *ed, Fl_Text_Buffer *buf)
{
    detach();
    editor_    = ed;
    buf_       = buf;
    style_buf_ = new Fl_Text_Buffer();

    const int tlen = buf_->length();
    if (tlen > 0) {
        std::string placeholder(static_cast<std::size_t>(tlen), S_PLAIN);
        style_buf_->text(placeholder.c_str());
    }
}

void Highlighter::detach()
{
    cancel_timer();
    pending_pos_ = -1;
    pending_end_ = 0;
    rescanner_.reset();
    code_styles_.clear();
    if (editor_) {
        editor_->highlight_data(nullptr, nullptr, 0, 0, nullptr, nullptr);
        editor_ = nullptr;
    }
    if (style_buf_) {
        delete style_buf_;
        style_buf_ = nullptr;
    }
    buf_ = nullptr;
}

void Highlighter::rebuild_style_table(Fl_Font font, Fl_Fontsize size, bool dark_mode)
{
    font_ = font;
    size_ = size;
    dark_ = dark_mode;
    if (!editor_ || !style_buf_) return;

    if (mode_ == HighlightMode::Code) {
        build_code_table();
        table_size_ = 6;
    } else {
        build_markdown_table();
        table_size_ = N_MD_STYLES;
    }
    editor_->highlight_data(style_buf_, table_, table_size_, S_PLAIN,
                            nullptr, nullptr);
    editor_->redraw();
}

void Highlighter::build_markdown_table()
{
    const Fl_Font font = font_;
    const Fl_Fontsize size = size_;

    // Derive bold / italic variants.
    // FLTK built-in families (Helvetica=0, Courier=4, Times=8) use bit 0 for bold
    // and bit 1 for italic within their group of four.  System fonts (>= 16) have
    // no guaranteed variants, so fall back to the plain face.
    const bool   has_variants = (font <= 11 && (font & 3) == 0);
    const Fl_Font bold_font   = has_variants ? static_cast<Fl_Font>(font | 1) : font;
    const Fl_Font italic_font = has_variants ? static_cast<Fl_Font>(font | 2) : font;

    Fl_Color plain_col, heading_col, strong_col, em_col,
             code_col, fence_col, link_col, quote_col,
             list_col, special_col;

    if (dark_) {
        plain_col   = fl_rgb_color(220, 220, 220);
        heading_col = fl_rgb_color( 86, 156, 214);   // #569cd6 – VS Code keyword blue
        strong_col  = fl_rgb_color(255, 255, 255);
        em_col      = fl_rgb_color(206, 180, 140);   // warm parchment
        code_col    = fl_rgb_color(206, 145, 120);   // #ce9178 – VS Code string orange
        fence_col   = fl_rgb_color(206, 145, 120);
        link_col    = fl_rgb_color( 78, 201, 176);   // #4ec9b0 – VS Code teal
        quote_col   = fl_rgb_color(106, 153,  85);   // #6a9955 – VS Code comment green
        list_col    = fl_rgb_color( 86, 156, 214);
        special_col = fl_rgb_color(128, 128, 128);
    } else {
        plain_col   = FL_BLACK;
        heading_col = fl_rgb_color( 20,  80, 180);   // deep blue
        strong_col  = FL_BLACK;
        em_col      = FL_BLACK;
        code_col    = fl_rgb_color(160,  40,  40);   // dark red
        fence_col   = fl_rgb_color(160,  40,  40);
        link_col    = fl_rgb_color(  0, 100, 200);   // medium blue
        quote_col   = fl_rgb_color(100, 100, 100);   // gray
        list_col    = fl_rgb_color( 20,  80, 180);
        special_col = fl_rgb_color(150, 150, 150);
    }

    // Indices: style char - 'A'. Inline code gets a background behind the
    // span; fenced blocks extend it to the end of the line.
    const Fl_Color code_bg = dark_ ? fl_rgb_color(48, 48, 48)
                                   : fl_rgb_color(240, 240, 240);

    table_[0] = { plain_col,   font,        size, 0,
                  FL_BACKGROUND_COLOR };                                    // A plain
    table_[1] = { heading_col, bold_font,   size, 0,
                  FL_BACKGROUND_COLOR };                                    // B heading
    table_[2] = { strong_col,  bold_font,   size, 0,
                  FL_BACKGROUND_COLOR };                                    // C **bold**
    table_[3] = { em_col,      italic_font, size, 0,
                  FL_BACKGROUND_COLOR };                                    // D *italic*
    table_[4] = { code_col,    font,        size,
                  Fl_Text_Display::ATTR_BGCOLOR, code_bg };                 // E `code`
    table_[5] = { fence_col,   font,        size,
                  Fl_Text_Display::ATTR_BGCOLOR_EXT, code_bg };             // F ~~~ block
    table_[6] = { link_col,    font,        size,
                  Fl_Text_Display::ATTR_UNDERLINE,
                  FL_BACKGROUND_COLOR };                                    // G [link]
    table_[7] = { quote_col,   font,        size, 0,
                  FL_BACKGROUND_COLOR };                                    // H > quote
    table_[8] = { list_col,    bold_font,   size, 0,
                  FL_BACKGROUND_COLOR };                                    // I list marker
    table_[9] = { special_col, font,        size,
                  Fl_Text_Display::ATTR_STRIKE_THROUGH,
                  FL_BACKGROUND_COLOR };                                    // J ~~strike~~ / HR
}

void Highlighter::build_code_table()
{
    const Fl_Font font = font_;
    const Fl_Fontsize size = size_;

    const bool   has_variants = (font <= 11 && (font & 3) == 0);
    const Fl_Font bold_font   = has_variants ? static_cast<Fl_Font>(font | 1) : font;
    const Fl_Font italic_font = has_variants ? static_cast<Fl_Font>(font | 2) : font;

    Fl_Color plain_col, keyword_col, string_col, comment_col,
             number_col, preproc_col;

    if (dark_) {
        plain_col   = fl_rgb_color(212, 212, 212);
        keyword_col = fl_rgb_color( 86, 156, 214);
        string_col  = fl_rgb_color(206, 145, 120);
        comment_col = fl_rgb_color(106, 153,  85);
        number_col  = fl_rgb_color(181, 206, 168);
        preproc_col = fl_rgb_color(197, 134, 192);
    } else {
        plain_col   = FL_BLACK;
        keyword_col = fl_rgb_color( 20,  80, 180);
        string_col  = fl_rgb_color(160,  40,  40);
        comment_col = fl_rgb_color(  0, 120,   0);
        number_col  = fl_rgb_color(128,   0, 128);
        preproc_col = fl_rgb_color(150,  60, 150);
    }

    // Style char = 'A' + static_cast<int>(SynStyle).
    table_[0] = { plain_col,   font,        size, 0, FL_BACKGROUND_COLOR };  // Normal
    table_[1] = { keyword_col, bold_font,   size, 0, FL_BACKGROUND_COLOR };  // Keyword
    table_[2] = { string_col,  font,        size, 0, FL_BACKGROUND_COLOR };  // String
    table_[3] = { comment_col, italic_font, size, 0, FL_BACKGROUND_COLOR };  // Comment
    table_[4] = { number_col,  font,        size, 0, FL_BACKGROUND_COLOR };  // Number
    table_[5] = { preproc_col, font,        size, 0, FL_BACKGROUND_COLOR };  // Preproc
}

void Highlighter::set_mode(HighlightMode mode, SynLang lang)
{
    const SynLang effective = (mode == HighlightMode::Code) ? lang : SynLang::None;
    if (mode == mode_ && effective == lang_) return;

    mode_ = mode;
    lang_ = effective;
    cancel_timer();
    pending_pos_ = -1;
    pending_end_ = 0;
    rescanner_.set_language(lang_);
    rescanner_.reset();
    code_styles_.clear();
}

void Highlighter::rehighlight_all()
{
    if (!buf_ || !style_buf_) return;
    cancel_timer();
    pending_pos_ = -1;
    pending_end_ = 0;

    if (mode_ == HighlightMode::Code) {
        scan_code_all();
        return;
    }

    char *text = buf_->text();
    const int tlen = buf_->length();

    if (tlen == 0) {
        if (style_buf_->length() > 0)
            style_buf_->replace(0, style_buf_->length(), "");
        gui_secure_free_text(text, static_cast<std::size_t>(tlen));
        return;
    }

    std::string style(static_cast<std::size_t>(tlen), S_PLAIN);
    if (mode_ == HighlightMode::Markdown) scan(text, tlen, style.data());
    gui_secure_free_text(text, static_cast<std::size_t>(tlen));

    style_buf_->replace(0, style_buf_->length(), style.c_str());
}

void Highlighter::scan_code_all()
{
    const int tlen = buf_->length();
    rescanner_.set_language(lang_);

    if (tlen <= 0) {
        code_styles_.clear();
        rescanner_.reset();
        if (style_buf_->length() > 0)
            style_buf_->replace(0, style_buf_->length(), "");
        return;
    }

    char *raw = buf_->text();
    if (raw == nullptr) return;
    const std::string_view text(raw, static_cast<std::size_t>(tlen));

    rescanner_.rescan_all(text, code_styles_);

    std::string out(static_cast<std::size_t>(tlen), S_PLAIN);
    for (int i = 0; i < tlen; ++i) {
        out[static_cast<std::size_t>(i)] =
            static_cast<char>('A' + code_styles_[static_cast<std::size_t>(i)]);
    }
    gui_secure_free_text(raw, static_cast<std::size_t>(tlen));

    style_buf_->replace(0, style_buf_->length(), out.c_str());
}

void Highlighter::rescan_code_edit(int pos, int nIns, int nDel)
{
    const int tlen = buf_->length();
    if (tlen <= 0) {
        code_styles_.clear();
        rescanner_.reset();
        return;
    }
    if (style_buf_->length() != tlen) {
        // Defensive: the style buffer is length-synced in on_modify(), so this
        // only happens if a caller bypassed it. Fall back to a full scan.
        scan_code_all();
        return;
    }

    char *raw = buf_->text();
    if (raw == nullptr) return;
    const std::string_view text(raw, static_cast<std::size_t>(tlen));

    // The style buffer was length-synced in on_modify(); mirror it so the
    // rescanner sees the same starting point as the editor.
    char *sb = style_buf_->text();
    if (sb == nullptr) {
        gui_secure_free_text(raw, static_cast<std::size_t>(tlen));
        scan_code_all();
        return;
    }
    code_styles_.resize(static_cast<std::size_t>(tlen));
    for (std::size_t i = 0; i < code_styles_.size(); ++i) {
        const char c = sb[i];
        unsigned char v = (c >= 'A' && c <= 'Z')
                              ? static_cast<unsigned char>(c - 'A')
                              : static_cast<unsigned char>(SynStyle::Normal);
        // Only the six code styles are valid in this mode.
        if (v > static_cast<unsigned char>(SynStyle::Preproc))
            v = static_cast<unsigned char>(SynStyle::Normal);
        code_styles_[i] = v;
    }
    gui_secure_free_text(sb, static_cast<std::size_t>(tlen));

    const auto dirty = rescanner_.rescan_after_edit(
        text, static_cast<std::size_t>(pos), static_cast<std::size_t>(nDel),
        static_cast<std::size_t>(nIns), code_styles_);

    if (dirty.end > dirty.start) {
        std::string out(dirty.end - dirty.start, S_PLAIN);
        for (std::size_t i = dirty.start; i < dirty.end; ++i) {
            out[i - dirty.start] = static_cast<char>('A' + code_styles_[i]);
        }
        style_buf_->replace(static_cast<int>(dirty.start),
                            static_cast<int>(dirty.end), out.c_str());
    }

    gui_secure_free_text(raw, static_cast<std::size_t>(tlen));
}

void Highlighter::on_modify(int pos, int nIns, int nDel)
{
    if (!buf_ || !style_buf_) return;
    if (nIns == 0 && nDel == 0) return;

    // Step 1: keep style buffer length in sync with the text buffer immediately
    // so cursor offsets and the style index stay valid for paint events that
    // arrive before the deferred rescan runs.
    if (nDel > 0 && nIns == 0) {
        style_buf_->remove(pos, pos + nDel);
    } else if (nIns > 0 && nDel == 0) {
        const std::string ph(static_cast<std::size_t>(nIns), S_PLAIN);
        style_buf_->insert(pos, ph.c_str());
    } else {
        const std::string ph(static_cast<std::size_t>(nIns), S_PLAIN);
        style_buf_->replace(pos, pos + nDel, ph.c_str());
    }

    // Code mode rescans the affected region immediately: the incremental
    // scanner is bounded by the checkpoint interval, not the document size.
    if (mode_ == HighlightMode::Code) {
        rescan_code_edit(pos, nIns, nDel);
        return;
    }
    if (mode_ != HighlightMode::Markdown) return;

    // Step 2: coalesce the edit range and (re)arm a debounce timer.
    // Subsequent edits within ~30 ms cancel and reschedule, so a burst of
    // keystrokes triggers only a single rescan.
    const int change_end = pos + nIns;
    if (pending_pos_ < 0) {
        pending_pos_ = pos;
        pending_end_ = change_end;
    } else {
        pending_pos_ = std::min(pending_pos_, pos);
        pending_end_ = std::max(pending_end_, change_end);
    }

    if (!timer_scheduled_) {
        Fl::add_timeout(0.03, &Highlighter::timeout_cb, this);
        timer_scheduled_ = true;
    }
}

// ---------------------------------------------------------------------------
// Debounced rescan plumbing
// ---------------------------------------------------------------------------
void Highlighter::timeout_cb(void *self)
{
    static_cast<Highlighter *>(self)->flush_pending();
}

void Highlighter::cancel_timer()
{
    if (timer_scheduled_) {
        Fl::remove_timeout(&Highlighter::timeout_cb, this);
        timer_scheduled_ = false;
    }
}

void Highlighter::flush_pending()
{
    timer_scheduled_ = false;
    if (!buf_ || !style_buf_ || pending_pos_ < 0) return;

    const int pos = pending_pos_;
    const int end = pending_end_;
    pending_pos_ = -1;
    pending_end_ = 0;

    // Try the fast block-local path; fall back to a full rescan if any
    // construct that crosses paragraph boundaries (fenced code) is present.
    if (!try_block_rescan(pos, end)) {
        // rehighlight_all() touches pending_*, but we already cleared it.
        char *text = buf_->text();
        const int tlen = buf_->length();
        if (tlen > 0) {
            std::string style(static_cast<std::size_t>(tlen), S_PLAIN);
            scan(text, tlen, style.data());
            style_buf_->replace(0, style_buf_->length(), style.c_str());
        }
        gui_secure_free_text(text, static_cast<std::size_t>(tlen));
    }
}

bool Highlighter::has_fence_markers(const char *text, int len)
{
    int i = 0;
    while (i < len) {
        // Line start: scan up to 3 leading spaces, then check for 3+ ` or ~.
        int j = i;
        int spaces = 0;
        while (j < len && text[j] == ' ' && spaces < 3) { ++j; ++spaces; }
        if (j < len && (text[j] == '`' || text[j] == '~')) {
            const char fc = text[j];
            int n = 0;
            while (j < len && text[j] == fc) { ++n; ++j; }
            if (n >= 3) return true;
        }
        // Advance to next line
        while (i < len && text[i] != '\n') ++i;
        if (i < len) ++i;
    }
    return false;
}

bool Highlighter::try_block_rescan(int pos, int end)
{
    const int tlen = buf_->length();
    if (tlen == 0) {
        if (style_buf_->length() > 0)
            style_buf_->replace(0, style_buf_->length(), "");
        return true;
    }

    char *text = buf_->text();
    if (!text) return false;

    // Bail out to full rescan if the document contains any fenced code blocks.
    // Their style state spans many lines, so a local update could miss
    // openings or closings outside the edited paragraph.
    if (has_fence_markers(text, tlen)) {
        gui_secure_free_text(text, static_cast<std::size_t>(tlen));
        return false;
    }

    // Clamp the change range into the current buffer.
    int cp = std::max(0, std::min(pos, tlen));
    int ce = std::max(cp, std::min(end, tlen));

    // Block start: walk back from cp to the start of the paragraph.  A
    // paragraph boundary here is a blank line, i.e. a '\n' whose next char is
    // also '\n', OR the start of the buffer.  After locating it, bs lands on
    // the first char of the paragraph.
    int bs = cp;
    while (bs > 0) {
        // Step back to start of the current line.
        while (bs > 0 && text[bs - 1] != '\n') --bs;
        if (bs == 0) break;
        // bs is now at a line start; check if the previous line is blank.
        // Previous line is text[..bs-1] terminated by '\n' at bs-1.  Blank if
        // bs == 1 (just one '\n') or text[bs - 2] == '\n'.
        if (bs >= 2 && text[bs - 2] == '\n') break;
        if (bs == 1) break;
        // Not a boundary; keep walking back through the previous line.
        --bs;
    }

    // Block end: walk forward from ce to the end of the paragraph.  Stop at
    // the '\n' that precedes a blank line, or at end of buffer.  be is
    // exclusive (the position one past the last char to rescan).
    int be = ce;
    while (be < tlen) {
        // Advance to end of current line.
        while (be < tlen && text[be] != '\n') ++be;
        if (be >= tlen) break;
        // be now points at '\n'; check if the next char is also '\n'.
        if (be + 1 >= tlen || text[be + 1] == '\n') break;
        ++be;  // Skip the '\n' and continue with the next line.
    }

    // Scan only the block and splice into the style buffer.
    const int blen = be - bs;
    if (blen > 0) {
        std::string style(static_cast<std::size_t>(blen), S_PLAIN);
        scan(text + bs, blen, style.data());
        style_buf_->replace(bs, be, style.c_str());
    }

    gui_secure_free_text(text, static_cast<std::size_t>(tlen));
    return true;
}

// ---------------------------------------------------------------------------
// url_at: return the href for the link span covering character position pos.
// Returns empty string when pos is not over a styled link ('G') or when the
// text doesn't contain a recognisable inline-link pattern.
// ---------------------------------------------------------------------------
std::string Highlighter::url_at(int pos) const
{
    if (mode_ != HighlightMode::Markdown) return {};
    if (!style_buf_ || !buf_) return {};
    const int len = style_buf_->length();
    if (pos < 0 || pos >= len) return {};
    if (style_buf_->char_at(pos) != S_LINK) return {};

    // Expand to the full 'G'-styled run
    int start = pos;
    while (start > 0 && style_buf_->char_at(start - 1) == S_LINK) --start;
    int end = pos;
    while (end < len - 1 && style_buf_->char_at(end + 1) == S_LINK) ++end;
    ++end;  // exclusive

    char *raw = buf_->text_range(start, end);
    if (!raw) return {};
    std::string span(raw);
    gui_secure_free_text(raw, static_cast<std::size_t>(end - start));

    // Inline link: [text](url) or ![img](url)
    // Find the last "](" in the span so nested brackets don't confuse us.
    std::size_t bracket = span.rfind("](");
    if (bracket != std::string::npos) {
        std::size_t url_start = bracket + 2;
        std::size_t url_end   = span.find(')', url_start);
        if (url_end != std::string::npos) {
            std::string url = span.substr(url_start, url_end - url_start);
            // Strip optional title: [text](url "title")
            std::size_t space = url.find(' ');
            if (space != std::string::npos) url.resize(space);
            // Strip angle brackets: [text](<url>)
            if (url.size() >= 2 && url.front() == '<' && url.back() == '>')
                url = url.substr(1, url.size() - 2);
            return url;
        }
    }
    return {};
}
