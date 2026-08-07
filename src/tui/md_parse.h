#ifndef TUI_MD_PARSE_H
#define TUI_MD_PARSE_H

// Lightweight markdown classifier for terminal rendering.
//
// Ported in spirit from src/gui/md_highlight.cpp, but instead of writing an
// FLTK style buffer it emits style runs (byte offsets into the line) that the
// FTXUI view turns into coloured text spans. Fenced code-block state is tracked
// by the caller across lines (see md_code_fence / in_code argument).

#include <cstddef>
#include <string>
#include <vector>

enum class MdStyle {
    Normal,
    Heading,
    CodeBlock,
    InlineCode,
    Strong,
    Emphasis,
    Quote,
    ListMarker,
    Link,
    Rule,
};

struct MdRun {
    std::size_t start;  // byte offset into the line
    std::size_t len;    // byte length
    MdStyle style;
};

// True if `line` is a ``` or ~~~ fenced-code delimiter (toggles code state).
bool md_is_fence(const std::string &line);

// Classify one line. `in_code` is true when the line sits inside a fenced
// code block (the fence delimiters themselves are passed with in_code=false).
std::vector<MdRun> md_parse_line(const std::string &line, bool in_code);

#endif  // TUI_MD_PARSE_H
