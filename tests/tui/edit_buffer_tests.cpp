// Unit tests for EditBuffer — the FTXUI-free document model.

#include <cassert>
#include <cstdio>
#include <string>

#include "tui/edit_buffer.h"

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void test_set_get_text() {
    EditBuffer b;
    b.set_text("hello\nworld");
    CHECK(b.line_count() == 2);
    CHECK(b.line(0) == "hello");
    CHECK(b.line(1) == "world");
    CHECK(b.text() == "hello\nworld");

    // CRLF is normalised away in the model.
    b.set_text("a\r\nb\r\n");
    CHECK(b.line_count() == 3);
    CHECK(b.line(0) == "a");
    CHECK(b.line(1) == "b");
    CHECK(b.line(2) == "");
}

static void test_insert_and_newline() {
    EditBuffer b;
    b.set_text("");
    b.insert_text("abc");
    CHECK(b.text() == "abc");
    CHECK(b.cursor().col == 3);
    b.insert_newline();
    b.insert_text("def");
    CHECK(b.text() == "abc\ndef");
    CHECK(b.cursor().line == 1);
    CHECK(b.cursor().col == 3);
}

static void test_backspace_join() {
    EditBuffer b;
    b.set_text("ab\ncd");
    b.set_cursor(Position{1, 0});
    b.backspace();  // join line 1 into line 0
    CHECK(b.text() == "abcd");
    CHECK(b.cursor().line == 0);
    CHECK(b.cursor().col == 2);
}

static void test_selection_and_delete() {
    EditBuffer b;
    b.set_text("hello world");
    b.set_cursor(Position{0, 0});
    b.move_right(true);  // select 'h'
    b.move_right(true);  // select 'he'
    CHECK(b.has_selection());
    CHECK(b.selected_text() == "he");
    b.delete_selection();
    CHECK(b.text() == "llo world");
}

static void test_unicode_columns() {
    EditBuffer b;
    b.set_text("a\xC3\xA9z");  // a é z  (é is 2 bytes, 1 code point)
    CHECK(b.line_length(0) == 3);
    b.set_cursor(Position{0, 1});
    b.del();  // delete é
    CHECK(b.line(0) == "az");
}

static void test_undo_redo() {
    EditBuffer b;
    b.set_text("");
    b.insert_text("hello");
    b.insert_newline();
    b.insert_text("world");
    std::string before = b.text();
    b.undo();
    CHECK(b.text() != before);
    b.redo();
    CHECK(b.text() == before);
}

static void test_duplicate_and_remove_line() {
    EditBuffer b;
    b.set_text("one\ntwo");
    b.set_cursor(Position{0, 0});
    b.duplicate_line();
    CHECK(b.text() == "one\none\ntwo");
    b.set_cursor(Position{1, 0});
    b.remove_line();
    CHECK(b.text() == "one\ntwo");
}

static void test_indent_outdent() {
    EditBuffer b;
    b.set_text("x");
    b.indent(true, 4);  // spaces
    CHECK(b.line(0) == "    x");
    b.outdent(4);
    CHECK(b.line(0) == "x");
}

int main() {
    test_set_get_text();
    test_insert_and_newline();
    test_backspace_join();
    test_selection_and_delete();
    test_unicode_columns();
    test_undo_redo();
    test_duplicate_and_remove_line();
    test_indent_outdent();

    if (g_failures == 0) {
        std::printf("all EditBuffer tests passed\n");
        return 0;
    }
    std::printf("%d EditBuffer test(s) failed\n", g_failures);
    return 1;
}
