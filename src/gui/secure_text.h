#ifndef ENCRYPTED_GUI_SECURE_TEXT_H
#define ENCRYPTED_GUI_SECURE_TEXT_H

// Wiping helpers for document text held by Fl_Text_Buffer.
//
// Fl_Text_Buffer::text() and text_range() return heap copies of plaintext;
// release them through gui_secure_free_text() so the bytes are zeroed before
// the allocation returns to the allocator.
//
// Fl_Text_Buffer keeps its characters in one allocation with a movable gap.
// gui_wipe_text_buffer() zeroes the whole allocation (the gap included, where
// deleted text lingers) using address(), which is safe to call immediately
// before replacing or destroying the buffer.
//
// FLTK also keeps undo/redo action buffers and makes short-lived copies while
// editing. They are owned by the toolkit and not reachable through its public
// API, so this module cannot wipe them; treat the helpers as best effort.

#include <cstddef>
#include <cstdlib>

#include <FL/Fl_Text_Buffer.H>

extern "C" {
#include "platform/secure_mem.h"
}

inline void gui_secure_free_text(char *text, std::size_t len) {
    if (text == nullptr) return;
    txx_secure_zero(text, len);
    std::free(text);
}

// Zero the buffer's entire character allocation without changing its content.
// Returns the number of bytes wiped (0 for an empty buffer).
inline std::size_t gui_wipe_text_buffer(Fl_Text_Buffer &buffer) {
    const int len = buffer.length();
    if (len <= 0) return 0;
    // address(length) is one past the allocation: Fl_Text_Buffer allocates
    // exactly length + gap bytes, and the gap sits somewhere inside.
    char *first = buffer.address(0);
    char *last  = buffer.address(len);
    if (first == nullptr || last == nullptr || last < first) return 0;
    const std::size_t span = static_cast<std::size_t>(last - first);
    txx_secure_zero(first, span);
    return span;
}

// Empty the buffer, then zero the (still allocated) storage, then release it.
// The buffer is emptied through the normal path first: FLTK's display measures
// the outgoing text in its pre-delete callback, and feeding it zeroed bytes
// makes its UTF-8 composed-character scanner loop forever.
inline void gui_clear_text_buffer(Fl_Text_Buffer &buffer) {
    const int len = buffer.length();
    char *base = len > 0 ? buffer.address(0) : nullptr;
    char *end  = len > 0 ? buffer.address(len) : nullptr;
    const std::size_t span = (base != nullptr && end != nullptr && end > base)
                                 ? static_cast<std::size_t>(end - base)
                                 : 0;
    if (len > 0) buffer.remove(0, len);
    if (span != 0) txx_secure_zero(base, span);
    buffer.text("");  // frees the now-zeroed allocation
}

#endif  // ENCRYPTED_GUI_SECURE_TEXT_H
