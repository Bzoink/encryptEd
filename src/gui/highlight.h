#ifndef ENCRYPTED_GUI_HIGHLIGHT_H
#define ENCRYPTED_GUI_HIGHLIGHT_H

// Syntax highlighter for Fl_Text_Editor.
//
// Two modes share one style-buffer implementation:
//   * Markdown  - the rich inline scanner (headings, emphasis, links, fences).
//   * Code      - the shared tokenizer in highlight/syn_parse, rescanning
//                 incrementally through highlight::Rescanner.
//
// The style buffer holds one byte per text byte; the byte value minus 'A' is
// the index into the style table registered with the editor.

#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>

#include <string>
#include <vector>

#include "highlight/rescan.h"
#include "highlight/syn_parse.h"

enum class HighlightMode {
    Off,
    Markdown,
    Code,
};

class Highlighter {
public:
    Highlighter() = default;
    ~Highlighter();

    Highlighter(const Highlighter &) = delete;
    Highlighter &operator=(const Highlighter &) = delete;

    // Wire up to an editor and its text buffer.
    void attach(Fl_Text_Editor *ed, Fl_Text_Buffer *buf);

    // Remove style registration and free the style buffer.
    void detach();

    // Select what gets highlighted. Does not rescan; call rehighlight_all()
    // after changing the mode or language.
    void set_mode(HighlightMode mode, SynLang lang = SynLang::None);
    HighlightMode mode() const { return mode_; }
    SynLang language() const { return lang_; }

    // Exposed for tests and for callers that need the raw style bytes.
    Fl_Text_Buffer *style_buffer() const { return style_buf_; }

    // Rebuild color/font entries and re-register with the editor.
    // Call once after attach() and again whenever font, size, or theme changes.
    void rebuild_style_table(Fl_Font font, Fl_Fontsize size, bool dark_mode);

    // Re-scan the whole document. Call after loading new text into the buffer.
    void rehighlight_all();

    // Update highlighting after a text modification.
    // pos/nIns/nDel come directly from Fl_Text_Buffer's modify callback.
    void on_modify(int pos, int nIns, int nDel);

    // Return the URL string for the link span at character position pos, or
    // empty string if pos is not inside a [text](url) / ![img](url) link.
    // Always empty in Code mode.
    std::string url_at(int pos) const;

private:
    Fl_Text_Editor *editor_    = nullptr;
    Fl_Text_Buffer *buf_       = nullptr;
    Fl_Text_Buffer *style_buf_ = nullptr;

    HighlightMode mode_ = HighlightMode::Off;
    SynLang       lang_ = SynLang::None;

    Fl_Font     font_ = FL_COURIER;
    Fl_Fontsize size_ = 14;
    bool        dark_ = false;

    // ---- Markdown state ---------------------------------------------------
    int  pending_pos_     = -1;     // -1 == no edit pending
    int  pending_end_     =  0;
    bool timer_scheduled_ = false;

    static constexpr int N_MD_STYLES = 10;

    // Scan the full document and write style bytes into out[] (same length as text).
    static void scan(const char *text, int len, char *out);
    // Scan a single line for inline markup (called by scan()).
    static void scan_inline(const char *text, int len, char *out, char plain);

    static void timeout_cb(void *self);
    void cancel_timer();
    void flush_pending();
    bool try_block_rescan(int pos, int end);
    static bool has_fence_markers(const char *text, int len);

    // ---- Code state -------------------------------------------------------
    highlight::Rescanner      rescanner_;
    std::vector<unsigned char> code_styles_;  // one SynStyle per text byte

    void scan_code_all();
    void rescan_code_edit(int pos, int nIns, int nDel);

    // ---- Shared -----------------------------------------------------------
    Fl_Text_Display::Style_Table_Entry table_[N_MD_STYLES]{};
    int table_size_ = N_MD_STYLES;

    void build_markdown_table();
    void build_code_table();
};

#endif  // ENCRYPTED_GUI_HIGHLIGHT_H
