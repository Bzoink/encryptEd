#ifndef ENCRYPTED_GUI_SECURE_TEXT_H
#define ENCRYPTED_GUI_SECURE_TEXT_H

// Wiping free for document-text copies.
//
// Fl_Text_Buffer::text() and text_range() return heap copies of plaintext.
// Always release them through this helper so the bytes are zeroed before the
// allocation returns to the allocator.

#include <cstddef>
#include <cstdlib>

extern "C" {
#include "platform/secure_mem.h"
}

inline void gui_secure_free_text(char *text, std::size_t len) {
    if (text == nullptr) return;
    txx_secure_zero(text, len);
    std::free(text);
}

#endif  // ENCRYPTED_GUI_SECURE_TEXT_H
