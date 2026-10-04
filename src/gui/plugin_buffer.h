#ifndef ENCRYPTED_PLUGIN_BUFFER_H
#define ENCRYPTED_PLUGIN_BUFFER_H

#include <FL/Fl_Text_Buffer.H>
#include "gui/edit_history.h"
#include "plugins/plugins.h"
extern "C" {
#include "platform/secure_mem.h"
}
#include <cstdlib>
#include <cstring>

// The caller applies cursor/selection separately. One replace is one undo action.
inline bool apply_plugin_text(EditorTextBuffer &buffer, const plugins::Document &document)
{
    char *before = buffer.text();
    bool changed = document.text != before;
    txx_secure_zero(before, std::strlen(before));
    std::free(before);
    if (changed) {
        buffer.separate_history();
        buffer.replace(0, buffer.length(), document.text.c_str(), static_cast<int>(document.text.size()));
        buffer.separate_history();
    }
    return changed;
}
#endif
