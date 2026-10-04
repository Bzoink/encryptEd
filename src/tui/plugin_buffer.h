#ifndef TUI_PLUGIN_BUFFER_H
#define TUI_PLUGIN_BUFFER_H
#include "plugins/plugins.h"
#include "tui/edit_buffer.h"

inline plugins::Document plugin_document(const EditBuffer &buffer)
{
    plugins::Document document;
    document.text = buffer.text();
    document.cursor = buffer.byte_position(buffer.cursor());
    if (buffer.has_selection()) {
        document.selection_start = buffer.byte_position(buffer.selection_start());
        document.selection_end = buffer.byte_position(buffer.selection_end());
    } else document.selection_start = document.selection_end = document.cursor;
    return document;
}

// Only call on a successful runtime result. Position-only changes do not add undo.
inline bool apply_plugin_document(EditBuffer &buffer, const plugins::Document &document)
{
    bool changed = buffer.replace_text(document.text);
    buffer.set_cursor(buffer.position_from_byte(document.cursor));
    buffer.set_selection(buffer.position_from_byte(document.selection_start),
                         buffer.position_from_byte(document.selection_end));
    return changed;
}
#endif
