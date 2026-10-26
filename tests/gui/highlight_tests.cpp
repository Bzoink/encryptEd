// Buffer-level tests for the FLTK syntax highlighter.
//
// No widgets are constructed: Highlighter::attach accepts a null editor and
// only touches it when a style table is registered. This keeps the test
// runnable on headless machines.

#include <cstdio>
#include <string>

#include <FL/Fl_Text_Buffer.H>

#include "gui/highlight.h"

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

namespace {

char style_at(const Fl_Text_Buffer *sb, int pos) {
    return sb->char_at(pos);
}

void set_text(Fl_Text_Buffer &buf, Highlighter &hl, const char *text) {
    buf.text(text);
    hl.rehighlight_all();
}

// Mirror EncryptEdWindow::modify_cb: patch the buffer, then notify.
void edit(Fl_Text_Buffer &buf, Highlighter &hl, int pos, int del,
          const char *ins) {
    const int n = static_cast<int>(std::char_traits<char>::length(ins));
    buf.replace(pos, pos + del, ins);
    hl.on_modify(pos, n, del);
}

void test_code_basic() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Code, SynLang::C);

    const std::string text = "int x = 42; // note\n";
    set_text(buf, hl, text.c_str());

    const Fl_Text_Buffer *sb = hl.style_buffer();
    CHECK(sb != nullptr);
    CHECK(sb->length() == buf.length());

    CHECK(style_at(sb, 0) == 'B');   // int        -> Keyword
    CHECK(style_at(sb, 4) == 'A');   // x          -> Normal
    CHECK(style_at(sb, 8) == 'E');   // 42         -> Number
    CHECK(style_at(sb, 12) == 'D');  // // note    -> Comment
    CHECK(style_at(sb, static_cast<int>(text.size()) - 2) == 'D');  // 'e' in note
}

void test_code_block_comment() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Code, SynLang::C);

    set_text(buf, hl, "/* start\nint x;\n*/ int y;\n");

    const Fl_Text_Buffer *sb = hl.style_buffer();
    CHECK(style_at(sb, 0) == 'D');
    CHECK(style_at(sb, 10) == 'D');  // int inside the block comment
    CHECK(style_at(sb, 17) == 'D');  // */ closes the comment
    CHECK(style_at(sb, 20) == 'B');  // int after the close is a keyword
}

void test_edit_keeps_style_length() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Code, SynLang::Python);

    set_text(buf, hl, "x = 1\n");
    CHECK(hl.style_buffer()->length() == buf.length());

    edit(buf, hl, 5, 0, "\n");
    CHECK(hl.style_buffer()->length() == buf.length());

    edit(buf, hl, 2, 1, "");
    CHECK(hl.style_buffer()->length() == buf.length());

    edit(buf, hl, 0, 2, "yy");
    CHECK(hl.style_buffer()->length() == buf.length());
}

void test_code_recolors_after_edit() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Code, SynLang::C);

    set_text(buf, hl, "int a;\nint b;\n");

    // Insert an opening block comment before the first line.
    edit(buf, hl, 0, 0, "/*\n");
    CHECK(style_at(hl.style_buffer(), 0) == 'D');
    // Second line moved by 3 bytes and is now inside the comment.
    const int second = static_cast<int>(std::string("/*\nint b;\n").find("int b"));
    CHECK(style_at(hl.style_buffer(), second) == 'D');

    // Close it again.
    const int close = second;
    edit(buf, hl, close, 0, "*/\n");
    CHECK(style_at(hl.style_buffer(), second + 3) == 'B');
}

void test_markdown_mode() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Markdown);

    const std::string text = "# Title\nplain **bold**\n";
    set_text(buf, hl, text.c_str());

    const Fl_Text_Buffer *sb = hl.style_buffer();
    CHECK(style_at(sb, 0) == 'B');  // heading
    CHECK(hl.url_at(0).empty());
}

void test_markdown_link() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Markdown);

    const std::string text = "see [site](https://example.com) now\n";
    set_text(buf, hl, text.c_str());

    const std::size_t link = text.find("[site]");
    CHECK(hl.url_at(static_cast<int>(link)) == "https://example.com");
    CHECK(hl.url_at(0).empty());
}

void test_off_mode() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Off);

    set_text(buf, hl, "int x = 1; // note\n");
    const Fl_Text_Buffer *sb = hl.style_buffer();
    for (int i = 0; i < sb->length(); ++i) CHECK(style_at(sb, i) == 'A');
    CHECK(hl.url_at(0).empty());
}

void test_mode_switch_clears_state() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);

    hl.set_mode(HighlightMode::Code, SynLang::C);
    set_text(buf, hl, "/* open\n");
    CHECK(hl.style_buffer()->char_at(0) == 'D');

    hl.set_mode(HighlightMode::Markdown);
    CHECK(hl.language() == SynLang::None);
    hl.set_mode(HighlightMode::Code, SynLang::Python);
    CHECK(hl.language() == SynLang::Python);
    set_text(buf, hl, "x = 1\n");
    CHECK(hl.style_buffer()->char_at(0) == 'A');
}

void test_large_document_stays_correct() {
    Fl_Text_Buffer buf;
    Highlighter hl;
    hl.attach(nullptr, &buf);
    hl.set_mode(HighlightMode::Code, SynLang::C);

    std::string text;
    for (int i = 0; i < 300; ++i)
        text += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    set_text(buf, hl, text.c_str());

    // Edit the first line, then check a late line is still a keyword.
    edit(buf, hl, 0, 0, "/* x */\n");
    const std::size_t late = std::string("/* x */\n" + text).find("int v250");
    CHECK(style_at(hl.style_buffer(), static_cast<int>(late)) == 'B');
    CHECK(hl.style_buffer()->length() == buf.length());
}

}  // namespace

int main() {
    test_code_basic();
    test_code_block_comment();
    test_edit_keeps_style_length();
    test_code_recolors_after_edit();
    test_markdown_mode();
    test_markdown_link();
    test_off_mode();
    test_mode_switch_clears_state();
    test_large_document_stays_correct();

    if (g_failures == 0) {
        std::printf("all GUI highlighter tests passed\n");
        return 0;
    }
    std::printf("%d GUI highlighter test(s) failed\n", g_failures);
    return 1;
}
