#include "tui/edit_buffer.h"

#include <algorithm>

extern "C" {
#include "core/utf8.h"
}

namespace {
// Tags used to coalesce consecutive same-kind edits into one undo step.
enum EditTag {
    TAG_NONE = -1,
    TAG_TYPING = 1,
    TAG_DELETE,
    TAG_STRUCTURAL,  // anything that should always start a fresh undo step
};
}  // namespace

EditBuffer::EditBuffer() = default;

int EditBuffer::cp_length(const std::string &s) {
    int n = 0;
    const char *p = s.c_str();
    const char *end = p + s.size();
    while (p < end) {
        int len = 0;
        utf8_decode_next(p, end, &len);
        if (len <= 0) len = 1;
        p += len;
        ++n;
    }
    return n;
}

std::size_t EditBuffer::col_to_byte(const std::string &line, int col) {
    const char *p = line.c_str();
    const char *end = p + line.size();
    int c = 0;
    while (p < end && c < col) {
        int len = 0;
        utf8_decode_next(p, end, &len);
        if (len <= 0) len = 1;
        p += len;
        ++c;
    }
    return static_cast<std::size_t>(p - line.c_str());
}

int EditBuffer::byte_to_col(const std::string &line, std::size_t byte) {
    const char *p = line.c_str();
    const char *end = p + line.size();
    const char *target = p + std::min(byte, line.size());
    int c = 0;
    while (p < target && p < end) {
        int len = 0;
        utf8_decode_next(p, end, &len);
        if (len <= 0) len = 1;
        p += len;
        ++c;
    }
    return c;
}

int EditBuffer::line_length(int i) const {
    if (i < 0 || i >= line_count()) return 0;
    return cp_length(lines_[static_cast<std::size_t>(i)]);
}

void EditBuffer::set_text(const std::string &text) {
    lines_.clear();
    std::string cur;
    for (char ch : text) {
        if (ch == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines_.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
    lines_.push_back(cur);
    if (lines_.empty()) lines_.emplace_back();
    cursor_ = Position{};
    anchor_ = Position{};
    has_selection_ = false;
    undo_.clear();
    redo_.clear();
    last_tag_ = TAG_NONE;
}

std::string EditBuffer::text() const {
    std::string out;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i) out.push_back('\n');
        out += lines_[i];
    }
    return out;
}

Position EditBuffer::clamp(Position p) const {
    if (p.line < 0) p.line = 0;
    if (p.line >= line_count()) p.line = line_count() - 1;
    int maxcol = line_length(p.line);
    if (p.col < 0) p.col = 0;
    if (p.col > maxcol) p.col = maxcol;
    return p;
}

void EditBuffer::move_to(Position p, bool extend) {
    p = clamp(p);
    if (extend) {
        if (!has_selection_) {
            anchor_ = cursor_;
            has_selection_ = true;
        }
    } else {
        has_selection_ = false;
    }
    cursor_ = p;
    last_tag_ = TAG_NONE;  // navigation breaks edit coalescing
}

void EditBuffer::push_undo(int tag) {
    // Coalesce consecutive same-tag edits into a single undo step.
    if (tag != TAG_STRUCTURAL && tag == last_tag_ && !undo_.empty()) {
        redo_.clear();
        return;
    }
    undo_.push_back(Snapshot{lines_, cursor_, anchor_, has_selection_});
    if (undo_.size() > 500) undo_.erase(undo_.begin());
    redo_.clear();
    last_tag_ = tag;
}

std::string EditBuffer::selected_text() const {
    if (!has_selection()) return std::string();
    Position a = selection_start();
    Position b = selection_end();
    if (a.line == b.line) {
        const std::string &ln = lines_[static_cast<std::size_t>(a.line)];
        std::size_t ba = col_to_byte(ln, a.col);
        std::size_t bb = col_to_byte(ln, b.col);
        return ln.substr(ba, bb - ba);
    }
    std::string out;
    const std::string &first = lines_[static_cast<std::size_t>(a.line)];
    out += first.substr(col_to_byte(first, a.col));
    out.push_back('\n');
    for (int l = a.line + 1; l < b.line; ++l) {
        out += lines_[static_cast<std::size_t>(l)];
        out.push_back('\n');
    }
    const std::string &last = lines_[static_cast<std::size_t>(b.line)];
    out += last.substr(0, col_to_byte(last, b.col));
    return out;
}

void EditBuffer::replace_range(Position a, Position b, const std::string &with) {
    // Remove [a, b) then insert `with` (which may contain '\n') at a.
    std::string head = lines_[static_cast<std::size_t>(a.line)].substr(
        0, col_to_byte(lines_[static_cast<std::size_t>(a.line)], a.col));
    std::string tail = lines_[static_cast<std::size_t>(b.line)].substr(
        col_to_byte(lines_[static_cast<std::size_t>(b.line)], b.col));

    lines_.erase(lines_.begin() + a.line, lines_.begin() + b.line + 1);

    // Split `with` into segments by '\n'.
    std::vector<std::string> segs{std::string()};
    for (char ch : with) {
        if (ch == '\n') segs.emplace_back();
        else if (ch != '\r') segs.back().push_back(ch);
    }

    if (segs.size() == 1) {
        std::string merged = head + segs[0] + tail;
        lines_.insert(lines_.begin() + a.line, merged);
        cursor_ = Position{a.line, byte_to_col(head + segs[0], (head + segs[0]).size())};
    } else {
        std::vector<std::string> ins;
        ins.push_back(head + segs.front());
        for (std::size_t i = 1; i + 1 < segs.size(); ++i) ins.push_back(segs[i]);
        std::string lastseg = segs.back();
        int last_line = a.line + static_cast<int>(segs.size()) - 1;
        ins.push_back(lastseg + tail);
        lines_.insert(lines_.begin() + a.line, ins.begin(), ins.end());
        cursor_ = Position{last_line, cp_length(lastseg)};
    }
    has_selection_ = false;
}

void EditBuffer::delete_selection() {
    if (!has_selection()) return;
    push_undo(TAG_STRUCTURAL);
    Position a = selection_start();
    Position b = selection_end();
    replace_range(a, b, "");
}

void EditBuffer::insert_text(const std::string &utf8) {
    if (utf8.empty()) return;
    bool structural = utf8.find('\n') != std::string::npos || has_selection();
    push_undo(structural ? TAG_STRUCTURAL : TAG_TYPING);
    if (has_selection()) {
        Position a = selection_start();
        Position b = selection_end();
        replace_range(a, b, utf8);
        return;
    }
    if (overwrite_ && utf8.find('\n') == std::string::npos) {
        // Overwrite the code points covered by the inserted text on this line.
        int adv = cp_length(utf8);
        Position end{cursor_.line, std::min(cursor_.col + adv, line_length(cursor_.line))};
        replace_range(cursor_, end, utf8);
        return;
    }
    replace_range(cursor_, cursor_, utf8);
}

void EditBuffer::insert_newline() {
    push_undo(TAG_STRUCTURAL);
    if (has_selection()) {
        replace_range(selection_start(), selection_end(), "\n");
        return;
    }
    replace_range(cursor_, cursor_, "\n");
}

void EditBuffer::backspace() {
    if (has_selection()) { delete_selection(); return; }
    if (cursor_.col == 0 && cursor_.line == 0) return;
    push_undo(TAG_DELETE);
    Position from;
    if (cursor_.col > 0) {
        from = Position{cursor_.line, cursor_.col - 1};
    } else {
        from = Position{cursor_.line - 1, line_length(cursor_.line - 1)};
    }
    replace_range(from, cursor_, "");
}

void EditBuffer::del() {
    if (has_selection()) { delete_selection(); return; }
    int last = line_count() - 1;
    if (cursor_.line == last && cursor_.col == line_length(last)) return;
    push_undo(TAG_DELETE);
    Position to;
    if (cursor_.col < line_length(cursor_.line)) {
        to = Position{cursor_.line, cursor_.col + 1};
    } else {
        to = Position{cursor_.line + 1, 0};
    }
    replace_range(cursor_, to, "");
}

void EditBuffer::move_left(bool extend) {
    Position p = cursor_;
    if (p.col > 0) p.col--;
    else if (p.line > 0) { p.line--; p.col = line_length(p.line); }
    move_to(p, extend);
}

void EditBuffer::move_right(bool extend) {
    Position p = cursor_;
    if (p.col < line_length(p.line)) p.col++;
    else if (p.line < line_count() - 1) { p.line++; p.col = 0; }
    move_to(p, extend);
}

void EditBuffer::move_up(bool extend) { move_to(Position{cursor_.line - 1, cursor_.col}, extend); }
void EditBuffer::move_down(bool extend) { move_to(Position{cursor_.line + 1, cursor_.col}, extend); }
void EditBuffer::move_home(bool extend) { move_to(Position{cursor_.line, 0}, extend); }
void EditBuffer::move_end(bool extend) {
    move_to(Position{cursor_.line, line_length(cursor_.line)}, extend);
}
void EditBuffer::move_doc_start(bool extend) { move_to(Position{0, 0}, extend); }
void EditBuffer::move_doc_end(bool extend) {
    move_to(Position{line_count() - 1, line_length(line_count() - 1)}, extend);
}
void EditBuffer::move_page(int delta_lines, bool extend) {
    move_to(Position{cursor_.line + delta_lines, cursor_.col}, extend);
}

void EditBuffer::goto_line(int one_based_line) {
    int l = one_based_line - 1;
    if (l < 0) l = 0;
    if (l >= line_count()) l = line_count() - 1;
    move_to(Position{l, 0}, false);
}

void EditBuffer::select_all() {
    anchor_ = Position{0, 0};
    cursor_ = Position{line_count() - 1, line_length(line_count() - 1)};
    has_selection_ = true;
    last_tag_ = TAG_NONE;
}

void EditBuffer::indent(bool use_spaces, int width) {
    std::string unit = use_spaces ? std::string(static_cast<std::size_t>(width), ' ') : "\t";
    if (!has_selection()) {
        push_undo(TAG_STRUCTURAL);
        replace_range(cursor_, cursor_, unit);
        return;
    }
    push_undo(TAG_STRUCTURAL);
    int a = selection_start().line;
    int b = selection_end().line;
    for (int l = a; l <= b; ++l) {
        lines_[static_cast<std::size_t>(l)].insert(0, unit);
    }
    cursor_ = clamp(cursor_);
    anchor_ = clamp(anchor_);
}

void EditBuffer::outdent(int width) {
    push_undo(TAG_STRUCTURAL);
    int a = has_selection() ? selection_start().line : cursor_.line;
    int b = has_selection() ? selection_end().line : cursor_.line;
    for (int l = a; l <= b; ++l) {
        std::string &ln = lines_[static_cast<std::size_t>(l)];
        if (!ln.empty() && ln[0] == '\t') {
            ln.erase(0, 1);
        } else {
            std::size_t n = 0;
            while (n < ln.size() && n < static_cast<std::size_t>(width) && ln[n] == ' ') ++n;
            ln.erase(0, n);
        }
    }
    cursor_ = clamp(cursor_);
    anchor_ = clamp(anchor_);
}

void EditBuffer::duplicate_line() {
    push_undo(TAG_STRUCTURAL);
    int l = cursor_.line;
    lines_.insert(lines_.begin() + l + 1, lines_[static_cast<std::size_t>(l)]);
    cursor_ = Position{l + 1, cursor_.col};
    has_selection_ = false;
}

void EditBuffer::remove_line() {
    push_undo(TAG_STRUCTURAL);
    if (line_count() == 1) {
        lines_[0].clear();
        cursor_ = Position{0, 0};
    } else {
        lines_.erase(lines_.begin() + cursor_.line);
        if (cursor_.line >= line_count()) cursor_.line = line_count() - 1;
        cursor_.col = std::min(cursor_.col, line_length(cursor_.line));
    }
    has_selection_ = false;
}

void EditBuffer::undo() {
    if (undo_.empty()) return;
    redo_.push_back(Snapshot{lines_, cursor_, anchor_, has_selection_});
    Snapshot s = undo_.back();
    undo_.pop_back();
    lines_ = s.lines;
    cursor_ = s.cursor;
    anchor_ = s.anchor;
    has_selection_ = s.has_selection;
    last_tag_ = TAG_NONE;
}

void EditBuffer::redo() {
    if (redo_.empty()) return;
    undo_.push_back(Snapshot{lines_, cursor_, anchor_, has_selection_});
    Snapshot s = redo_.back();
    redo_.pop_back();
    lines_ = s.lines;
    cursor_ = s.cursor;
    anchor_ = s.anchor;
    has_selection_ = s.has_selection;
    last_tag_ = TAG_NONE;
}

std::size_t EditBuffer::char_count() const {
    std::size_t n = 0;
    for (const auto &l : lines_) n += static_cast<std::size_t>(cp_length(l));
    return n;
}

std::size_t EditBuffer::byte_count() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i) n += 1;  // '\n'
        n += lines_[i].size();
    }
    return n;
}
