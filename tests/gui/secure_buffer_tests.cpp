// Buffer-level tests for the plaintext wiping helpers.
//
// These run headless: no widgets are constructed, only Fl_Text_Buffer.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <FL/Fl_Text_Buffer.H>

#include "gui/edit_history.h"
#include "gui/secure_text.h"

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

namespace {

// The allocation span a buffer owns, including the gap.
std::pair<char *, std::size_t> allocation_span(Fl_Text_Buffer &buf) {
    const int len = buf.length();
    if (len <= 0) return {nullptr, 0};
    char *first = buf.address(0);
    char *last  = buf.address(len);  // one past the allocation
    return {first, static_cast<std::size_t>(last - first)};
}

bool contains(Fl_Text_Buffer &buf, const char *needle) {
    auto [base, span] = allocation_span(buf);
    if (base == nullptr) return false;
    const char *begin = base;
    const char *end   = base + span;
    return std::search(begin, end, needle, needle + std::strlen(needle)) != end;
}

void test_wipe_covers_gap() {
    Fl_Text_Buffer buf;
    buf.text("0123456789SECRET");
    // Deleting moves the gap over the removed bytes, so the plaintext stays in
    // the allocation even though length() no longer reports it.
    buf.remove(10, 16);

    CHECK(buf.length() == 10);
    CHECK(contains(buf, "SECRET"));  // still present in the gap

    const std::size_t wiped = gui_wipe_text_buffer(buf);
    CHECK(wiped >= 16);

    auto [base, span] = allocation_span(buf);
    CHECK(base != nullptr);
    bool all_zero = true;
    for (std::size_t i = 0; i < span; ++i) {
        if (base[i] != 0) { all_zero = false; break; }
    }
    CHECK(all_zero);
    CHECK(!contains(buf, "SECRET"));

    buf.text("");
    CHECK(buf.length() == 0);
}

void test_wipe_empty_buffer() {
    Fl_Text_Buffer buf;
    CHECK(gui_wipe_text_buffer(buf) == 0);
    gui_clear_text_buffer(buf);
    CHECK(buf.length() == 0);
}

void test_clear_empties_buffer() {
    Fl_Text_Buffer buf;
    buf.text("secret");
    gui_clear_text_buffer(buf);
    CHECK(buf.length() == 0);
}

void test_editor_buffer_wipe_all() {
    EditorTextBuffer buf;
    buf.text("top secret");
    CHECK(buf.length() == 10);

    buf.wipe_all();
    CHECK(buf.length() == 0);
    CHECK(!buf.can_undo());
    CHECK(!buf.can_redo());
}

void test_editor_buffer_history_wiped() {
    EditorTextBuffer buf;
    buf.text("one");
    buf.insert(3, " two");
    buf.separate_history();  // move the undo list into a stored snapshot
    CHECK(buf.length() == 7);

    // With the snapshot present, undo would restore "one".
    buf.wipe_history();
    int cursor = 7;
    CHECK(buf.undo(&cursor) == 0);
    CHECK(buf.length() == 7);
}

void test_text_replacement_wipes_old_allocation() {
    // EditorTextBuffer::text() must zero the outgoing document before FLTK
    // frees it. We can only observe that the new content is correct and the
    // buffer remains usable; the wiping itself is covered above.
    EditorTextBuffer buf;
    buf.text("first document with secrets");
    buf.text("second");
    CHECK(buf.length() == 6);
    char *text = buf.text();
    CHECK(std::string(text) == "second");
    gui_secure_free_text(text, 6);
}

}  // namespace

int main() {
    test_wipe_covers_gap();
    test_wipe_empty_buffer();
    test_clear_empties_buffer();
    test_editor_buffer_wipe_all();
    test_editor_buffer_history_wiped();
    test_text_replacement_wipes_old_allocation();

    if (g_failures == 0) {
        std::printf("all secure buffer tests passed\n");
        return 0;
    }
    std::printf("%d secure buffer test(s) failed\n", g_failures);
    return 1;
}
