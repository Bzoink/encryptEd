#ifndef TUI_EDIT_BUFFER_H
#define TUI_EDIT_BUFFER_H

// EditBuffer: the document model for the text-mode editor.
//
// UI-agnostic by design (no FTXUI dependency) so it can be unit-tested
// standalone, mirroring how text_transform.cpp is testable without FLTK.
// Text is stored as a vector of UTF-8 lines with no embedded newline bytes;
// the line terminator style is the caller's concern (see App/save path).
//
// Cursor and selection positions are expressed in (line, column) where column
// counts Unicode code points within the line (a tab is one column).

#include <cstddef>
#include <string>
#include <vector>

struct Position {
    int line = 0;
    int col = 0;  // code-point index within the line

    bool operator==(const Position &o) const { return line == o.line && col == o.col; }
    bool operator!=(const Position &o) const { return !(*this == o); }
    bool operator<(const Position &o) const {
        return line != o.line ? line < o.line : col < o.col;
    }
};

class EditBuffer {
public:
    EditBuffer();
    ~EditBuffer();

    // Whole-document access. set_text() splits on '\n'; any trailing '\r'
    // is stripped from each line so the model is newline-agnostic.
    void set_text(const std::string &text);
    void clear_sensitive();
    // Replace as one undo action without resetting earlier history.
    bool replace_text(const std::string &text);
    std::size_t byte_position(Position p) const;
    Position position_from_byte(std::size_t byte) const;
    void set_selection(Position first, Position last) {
        anchor_ = clamp(first); selection_cursor_ = clamp(last);
        has_selection_ = anchor_ != selection_cursor_;
    }
    std::string text() const;  // lines joined with '\n'

    const std::vector<std::string> &lines() const { return lines_; }
    int line_count() const { return static_cast<int>(lines_.size()); }
    const std::string &line(int i) const { return lines_[static_cast<std::size_t>(i)]; }

    // Code-point length of a line.
    int line_length(int i) const;

    Position cursor() const { return cursor_; }
    void set_cursor(Position p) { cursor_ = clamp(p); }

    // Move the cursor to `p` while extending the selection (anchored at the
    // previous cursor position). Used for mouse drag-select.
    void extend_cursor(Position p) {
        if (!has_selection_) { anchor_ = cursor_; has_selection_ = true; }
        cursor_ = clamp(p);
        selection_cursor_ = cursor_;
    }

    bool has_selection() const { return has_selection_ && selection_start() != selection_end(); }
    Position selection_start() const { return selection_cursor_ < anchor_ ? selection_cursor_ : anchor_; }
    Position selection_end() const { return selection_cursor_ < anchor_ ? anchor_ : selection_cursor_; }
    void clear_selection() { has_selection_ = false; }
    std::string selected_text() const;

    bool overwrite() const { return overwrite_; }
    void toggle_overwrite() { overwrite_ = !overwrite_; }

    // Editing primitives. `extend` keeps/extends the selection for cursor moves.
    void insert_text(const std::string &utf8);  // may contain '\n'
    void insert_newline();
    void backspace();
    void del();
    void delete_selection();

    // Cursor movement.
    void move_left(bool extend);
    void move_right(bool extend);
    void move_up(bool extend);
    void move_down(bool extend);
    void move_home(bool extend);
    void move_end(bool extend);
    void move_doc_start(bool extend);
    void move_doc_end(bool extend);
    void move_page(int delta_lines, bool extend);
    void goto_line(int one_based_line);
    void select_all();

    // Indentation honouring the active mode/width (tabs vs. spaces).
    void indent(bool use_spaces, int width);
    void outdent(int width);

    // Line operations (map to the Edit -> Text menu).
    void duplicate_line();
    void remove_line();

    // Undo / redo.
    void undo();
    void redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    // Counts for the status bar.
    std::size_t char_count() const;  // code points, excluding line breaks
    std::size_t byte_count() const;  // UTF-8 bytes including '\n' between lines

private:
    struct Snapshot {
        std::vector<std::string> lines;
        Position cursor;
        Position anchor;
        Position selection_cursor;
        bool has_selection;
    };

    std::vector<std::string> lines_{std::string()};
    Position cursor_{};
    Position anchor_{};
    Position selection_cursor_{};
    bool has_selection_ = false;
    bool overwrite_ = false;

    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    // Coalescing tag: consecutive edits with the same tag share one undo step.
    int last_tag_ = -1;

    Position clamp(Position p) const;
    void move_to(Position p, bool extend);
    void push_undo(int tag);
    static void wipe_lines(std::vector<std::string> &lines);
    static void wipe_history(std::vector<Snapshot> &history);
    void replace_range(Position a, Position b, const std::string &with);

    // Code-point <-> byte index conversions within a single line's UTF-8 string.
    static std::size_t col_to_byte(const std::string &line, int col);
    static int byte_to_col(const std::string &line, std::size_t byte);
    static int cp_length(const std::string &s);
};

#endif  // TUI_EDIT_BUFFER_H
