#include <cassert>
#include <cstdio>
#include <string>

#include "gui/text_transform.h"

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        std::string _a = (a); \
        std::string _b = (b); \
        if (_a != _b) { \
            fprintf(stderr, "FAIL %s:%d: %s != %s\n  got:      \"%s\"\n  expected: \"%s\"\n", \
                    __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str()); \
            ++failures; \
        } \
    } while (0)

// ---------------------------------------------------------------------------
// tt_find_line_break
// ---------------------------------------------------------------------------

static void test_find_line_break()
{
    std::size_t end = 0, nl = 0;

    tt_find_line_break("hello\nworld", 0, end, nl);
    CHECK(end == 5 && nl == 1);

    tt_find_line_break("hello\r\nworld", 0, end, nl);
    CHECK(end == 5 && nl == 2);

    tt_find_line_break("hello\rworld", 0, end, nl);
    CHECK(end == 5 && nl == 1);

    // No line ending
    tt_find_line_break("noeol", 0, end, nl);
    CHECK(end == 5 && nl == 0);

    // Start at second line
    tt_find_line_break("hello\nworld", 6, end, nl);
    CHECK(end == 11 && nl == 0);

    // Empty string
    tt_find_line_break("", 0, end, nl);
    CHECK(end == 0 && nl == 0);
}

// ---------------------------------------------------------------------------
// tt_tabs_to_spaces
// ---------------------------------------------------------------------------

static void test_tabs_to_spaces()
{
    // Single tab at column 0 with tab_width=4
    CHECK_EQ(tt_tabs_to_spaces("\t", 4), "    ");

    // Tab mid-line: at column 2, tab_width=4 -> 2 spaces to reach column 4
    CHECK_EQ(tt_tabs_to_spaces("ab\t", 4), "ab  ");

    // Two tabs
    CHECK_EQ(tt_tabs_to_spaces("\t\t", 4), "        ");

    // No tabs -> unchanged
    CHECK_EQ(tt_tabs_to_spaces("hello", 4), "hello");

    // Newline resets column
    CHECK_EQ(tt_tabs_to_spaces("ab\n\t", 4), "ab\n    ");

    // Initial column offset: starting at column 2, tab at width 4 -> 2 spaces
    CHECK_EQ(tt_tabs_to_spaces("\t", 4, 2), "  ");

    // CRLF passes through
    CHECK_EQ(tt_tabs_to_spaces("\t\r\n\t", 4), "    \r\n    ");
}

// ---------------------------------------------------------------------------
// tt_spaces_to_tabs
// ---------------------------------------------------------------------------

static void test_spaces_to_tabs()
{
    // 4 leading spaces -> 1 tab
    CHECK_EQ(tt_spaces_to_tabs("    hello", 4), "\thello");

    // 8 leading spaces -> 2 tabs
    CHECK_EQ(tt_spaces_to_tabs("        hello", 4), "\t\thello");

    // 2 leading spaces (not a full stop) -> stay as spaces
    CHECK_EQ(tt_spaces_to_tabs("  hello", 4), "  hello");

    // 6 leading spaces -> 1 tab + 2 spaces
    CHECK_EQ(tt_spaces_to_tabs("      hello", 4), "\t  hello");

    // Non-leading spaces are untouched
    CHECK_EQ(tt_spaces_to_tabs("a    b", 4), "a    b");

    // Multiple lines
    CHECK_EQ(tt_spaces_to_tabs("    a\n    b", 4), "\ta\n\tb");

    // Empty input
    CHECK_EQ(tt_spaces_to_tabs("", 4), "");
}

// ---------------------------------------------------------------------------
// tt_trim_trailing
// ---------------------------------------------------------------------------

static void test_trim_trailing()
{
    CHECK_EQ(tt_trim_trailing("hello   \n"), "hello\n");
    CHECK_EQ(tt_trim_trailing("hello"), "hello");
    CHECK_EQ(tt_trim_trailing("  \n  \n"), "\n\n");
    CHECK_EQ(tt_trim_trailing("a  \r\nb  \r\n"), "a\r\nb\r\n");
    // No trailing whitespace -> unchanged
    CHECK_EQ(tt_trim_trailing("hello\n"), "hello\n");
    // Trailing tabs
    CHECK_EQ(tt_trim_trailing("x\t\t\n"), "x\n");
    // Empty input
    CHECK_EQ(tt_trim_trailing(""), "");
}

// ---------------------------------------------------------------------------
// tt_remove_blank_lines
// ---------------------------------------------------------------------------

static void test_remove_blank_lines()
{
    CHECK_EQ(tt_remove_blank_lines("a\n\nb"), "a\nb");
    CHECK_EQ(tt_remove_blank_lines("a\n   \nb"), "a\nb");
    CHECK_EQ(tt_remove_blank_lines("\na\n"), "a\n");
    // Blank-only input -> empty output
    CHECK_EQ(tt_remove_blank_lines("   \n"), "");
    // No blank lines -> unchanged
    CHECK_EQ(tt_remove_blank_lines("a\nb\n"), "a\nb\n");
    // CRLF style
    CHECK_EQ(tt_remove_blank_lines("a\r\n\r\nb"), "a\r\nb");
    // Empty input
    CHECK_EQ(tt_remove_blank_lines(""), "");
}

// ---------------------------------------------------------------------------
// tt_wrap_columns
// ---------------------------------------------------------------------------

static void test_wrap_columns()
{
    // Short line -> unchanged
    CHECK_EQ(tt_wrap_columns("hello world", 20), "hello world");

    // Line exactly at width -> unchanged
    CHECK_EQ(tt_wrap_columns("hello world", 11), "hello world");

    // Wrap at space: "hello world" at width 8 must break somewhere
    {
        std::string w = tt_wrap_columns("hello world", 8);
        CHECK(w.find('\n') != std::string::npos);
        // Content preserved (minus the inserted newline replacing a space)
        CHECK(w == "hello\nworld");
    }

    // width <= 0 -> passthrough
    CHECK_EQ(tt_wrap_columns("hello world", 0), "hello world");
    CHECK_EQ(tt_wrap_columns("hello world", -1), "hello world");

    // Preserves CRLF style
    {
        std::string crlf = tt_wrap_columns("hello world\r\n", 8);
        CHECK(crlf.find("\r\n") != std::string::npos);
    }

    // Already-wrapped line not wrapped again
    CHECK_EQ(tt_wrap_columns("hi\nthere", 10), "hi\nthere");

    // Empty input
    CHECK_EQ(tt_wrap_columns("", 20), "");
}

// ---------------------------------------------------------------------------
// tt_ensure_trailing_newline
// ---------------------------------------------------------------------------

static void test_ensure_trailing_newline()
{
    // No newline -> add LF
    CHECK_EQ(tt_ensure_trailing_newline("hello"), "hello\n");
    // Already ends with LF -> unchanged
    CHECK_EQ(tt_ensure_trailing_newline("hello\n"), "hello\n");
    // Already ends with CRLF -> unchanged
    CHECK_EQ(tt_ensure_trailing_newline("hello\r\n"), "hello\r\n");
    // Already ends with CR -> unchanged
    CHECK_EQ(tt_ensure_trailing_newline("hello\r"), "hello\r");
    // CRLF-style file gets CRLF appended
    CHECK_EQ(tt_ensure_trailing_newline("a\r\nb"), "a\r\nb\r\n");
    // CR-style file gets CR appended
    CHECK_EQ(tt_ensure_trailing_newline("a\rb"), "a\rb\r");
    // Empty string -> unchanged
    CHECK_EQ(tt_ensure_trailing_newline(""), "");
}

// ---------------------------------------------------------------------------

int main()
{
    test_find_line_break();
    test_tabs_to_spaces();
    test_spaces_to_tabs();
    test_trim_trailing();
    test_remove_blank_lines();
    test_wrap_columns();
    test_ensure_trailing_newline();

    if (failures == 0) {
        printf("All text_transform tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed.\n", failures);
    return 1;
}
