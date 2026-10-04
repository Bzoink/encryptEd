#include "gui/plugin_buffer.h"
#include <cstdio>

static bool equals(Fl_Text_Buffer &buffer, const std::string &expected)
{
    char *text = buffer.text();
    bool same = expected == text;
    txx_secure_zero(text, std::strlen(text));
    std::free(text);
    return same;
}

int main()
{
    EditorTextBuffer buffer;
    buffer.text("original");
    plugins::Document d;
    d.text = "changed\né";
    if (!apply_plugin_text(buffer, d)) return 1;
    if (apply_plugin_text(buffer, d)) return 2;
    if (!buffer.undo()) return 3;
    d.text = "original";
    if (apply_plugin_text(buffer, d)) return 4;
    if (!buffer.redo()) return 5;
    d.text = "changed\né";
    if (apply_plugin_text(buffer, d)) return 6;
    // A plugin must stay separate from preceding keyboard edits, even when
    // the document was just emptied and the plugin only inserts text.
    for (bool empty : {false, true}) {
        EditorTextBuffer previous;
        previous.text("before");
        if (empty) previous.remove(0, previous.length());
        else previous.insert(previous.length(), " typed");
        d.text = "plugin";
        apply_plugin_text(previous, d);
        if (!previous.undo() || !equals(previous, empty ? "" : "before typed")) return 7;
        if (!previous.undo() || !equals(previous, "before")) return 8;
    }
    EditorTextBuffer history;
    history.text("a");
    history.insert(1, "b");
    d.text = "P"; apply_plugin_text(history, d);
    history.insert(1, "!");
    d.text = "Q"; apply_plugin_text(history, d);
    for (const char *expected : {"P!", "P", "ab", "a"})
        if (!history.undo() || !equals(history, expected)) return 9;
    if (history.can_undo()) return 10;
    for (const char *expected : {"ab", "P", "P!", "Q"})
        if (!history.redo() || !equals(history, expected)) return 11;
    if (history.can_redo()) return 12;
    history.undo(); history.undo(); // Back to P; typing must drop both redo segments.
    d.text = "P";
    if (apply_plugin_text(history, d) || !history.can_redo()) return 13;
    history.insert(1, "?");
    if (history.can_redo() || !history.undo() || !equals(history, "P")) return 14;
    d.text = "new branch"; apply_plugin_text(history, d);
    if (history.can_redo()) return 15;
    if (!history.undo() || !equals(history, "P")) return 16;
    if (!history.undo() || !equals(history, "ab")) return 17;
    history.text("new document");
    if (history.can_undo() || history.can_redo()) return 18;
    std::puts("Plugin buffer undo/redo tests passed.");
    return 0;
}
