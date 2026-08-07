// Markdown syntax highlighter for Fl_Text_Editor.
// Manages a parallel style buffer that is kept in sync with text edits.
#pragma once
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>

class MdHighlighter {
public:
    MdHighlighter() = default;
    ~MdHighlighter();

    MdHighlighter(const MdHighlighter &) = delete;
    MdHighlighter &operator=(const MdHighlighter &) = delete;

    // Wire up to an editor and its text buffer.
    // Must be called before rebuild_style_table().
    void attach(Fl_Text_Editor *ed, Fl_Text_Buffer *buf);

    // Remove style registration and free the style buffer.
    void detach();

    // Rebuild color/font entries and re-register with the editor.
    // Call once after attach() and again whenever font, size, or theme changes.
    void rebuild_style_table(Fl_Font font, Fl_Fontsize size, bool dark_mode);

    // Re-scan the whole document.  Call after loading new text into the buffer.
    void rehighlight_all();

    // Update highlighting after a text modification.
    // pos/nIns/nDel come directly from Fl_Text_Buffer's modify callback.
    void on_modify(int pos, int nIns, int nDel);

    // Return the URL string for the link span at character position pos, or
    // empty string if pos is not inside a [text](url) / ![img](url) link.
    std::string url_at(int pos) const;

private:
    Fl_Text_Editor *editor_    = nullptr;
    Fl_Text_Buffer *buf_       = nullptr;
    Fl_Text_Buffer *style_buf_ = nullptr;

    // Pending edit range coalesced across keystrokes for debounced rescan.
    int  pending_pos_     = -1;     // -1 == no edit pending
    int  pending_end_     =  0;
    bool timer_scheduled_ = false;

    static constexpr int N_STYLES = 10;
    Fl_Text_Display::Style_Table_Entry table_[N_STYLES]{};

    // Scan the full document and write style bytes into out[] (same length as text).
    static void scan(const char *text, int len, char *out);
    // Scan a single line for inline markup (called by scan()).
    static void scan_inline(const char *text, int len, char *out, char plain);

    // Debounced-rescan plumbing
    static void timeout_cb(void *self);
    void cancel_timer();
    void flush_pending();
    // Try to rescan only the paragraph block around [pos, end).  Returns false
    // if a global rescan is required (e.g. doc contains a fenced code block).
    bool try_block_rescan(int pos, int end);
    // Cheap O(N) check: does the document contain any line that opens or
    // closes a fenced code block?  Used to decide between block-local and
    // full rescan paths.
    static bool has_fence_markers(const char *text, int len);
};
