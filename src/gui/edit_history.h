#ifndef ENCRYPTED_EDIT_HISTORY_H
#define ENCRYPTED_EDIT_HISTORY_H

#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <memory>
#include <vector>
#include <utility>

// FLTK coalesces adjacent edits, including an insertion after a deletion.
// Keep its native undo histories in separate segments around plugin commands.
// Only protected history pointers are exchanged; their opaque contents remain
// owned and interpreted by FLTK. Ordinary typing keeps FLTK's usual coalescing.
class EditorTextBuffer : public Fl_Text_Buffer {
    std::vector<std::unique_ptr<EditorTextBuffer>> past_, future_;
    bool replaying_ = false;

    void swap_history(EditorTextBuffer &other)
    {
        std::swap(mUndo, other.mUndo);
        std::swap(mUndoList, other.mUndoList);
        std::swap(mRedoList, other.mRedoList);
    }

    void discard_redo()
    {
        future_.clear();
        if (Fl_Text_Buffer::can_redo()) {
            EditorTextBuffer empty;
            std::swap(mRedoList, empty.mRedoList);
        }
    }

    static void modified(int, int inserted, int deleted, int, const char *, void *data)
    {
        auto &self = *static_cast<EditorTextBuffer *>(data);
        if (!self.replaying_ && (inserted || deleted)) self.discard_redo();
    }

    static int history_key(Fl_Text_Editor *editor, bool redo)
    {
        auto &buffer = *static_cast<EditorTextBuffer *>(editor->buffer());
        buffer.unselect();
        int cursor = editor->insert_position();
        int changed = redo ? buffer.redo(&cursor) : buffer.undo(&cursor);
        editor->insert_position(cursor);
        editor->show_insert_position();
        if (changed) {
            editor->set_changed();
            if (editor->when() & FL_WHEN_CHANGED) editor->do_callback();
        }
        return 1;
    }

public:
    EditorTextBuffer() { add_modify_callback(modified, this); }
    ~EditorTextBuffer() { remove_modify_callback(modified, this); }

    using Fl_Text_Buffer::text;
    void text(const char *value)
    {
        Fl_Text_Buffer::text(value);
        past_.clear(); future_.clear();
        EditorTextBuffer empty;
        swap_history(empty);
    }

    // Call on both sides of a committed plugin change. No-op plugins do not
    // create a boundary, so they leave undo/redo history entirely untouched.
    void separate_history()
    {
        discard_redo();
        if (!Fl_Text_Buffer::can_undo()) return;
        auto history = std::make_unique<EditorTextBuffer>();
        swap_history(*history);
        past_.push_back(std::move(history));
    }

    bool can_undo() const { return Fl_Text_Buffer::can_undo() || !past_.empty(); }
    bool can_redo() const { return Fl_Text_Buffer::can_redo() || !future_.empty(); }

    int undo(int *cursor = nullptr)
    {
        if (!Fl_Text_Buffer::can_undo() && !past_.empty()) {
            auto history = std::move(past_.back()); past_.pop_back();
            swap_history(*history);
            if (history->Fl_Text_Buffer::can_redo()) future_.push_back(std::move(history));
        }
        replaying_ = true;
        int result = Fl_Text_Buffer::undo(cursor);
        replaying_ = false;
        return result;
    }

    int redo(int *cursor = nullptr)
    {
        if (!Fl_Text_Buffer::can_redo() && !future_.empty()) {
            auto history = std::move(future_.back()); future_.pop_back();
            swap_history(*history);
            if (history->Fl_Text_Buffer::can_undo()) past_.push_back(std::move(history));
        }
        replaying_ = true;
        int result = Fl_Text_Buffer::redo(cursor);
        replaying_ = false;
        return result;
    }

    static int undo_key(int, Fl_Text_Editor *editor) { return history_key(editor, false); }
    static int redo_key(int, Fl_Text_Editor *editor) { return history_key(editor, true); }
};
#endif
