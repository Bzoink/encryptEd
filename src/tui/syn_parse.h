#ifndef TUI_SYN_PARSE_H
#define TUI_SYN_PARSE_H

// Syntax highlighter for the terminal editor.
//
// Same shape as md_parse.h: syn_parse_line() returns a list of non-overlapping
// byte-range style runs for one line, updating SynState for multi-line constructs
// (block comments, Python triple-quoted strings). The caller walks lines top-to-
// bottom accumulating state, so inter-line context is O(N) per frame total.

#include <cstddef>
#include <string>
#include <vector>

enum class SynLang {
    None,
    C,
    Cpp,
    Python,
    Shell,
    CMake,
    Rust,
    JavaScript,
    HTML,
    CSS,
    PowerShell,
    Go,
    PHP,
    JSON,
    SQL,
    Dockerfile,
    YAML,
    XML,
    Java,
    CSharp,
    Markdown,
    Makefile,
    Batch,
    Ruby,
    Perl,
    TOML,
    INI,
    R,
    Swift,
    Kotlin,
    Lua,
    Asm,
};

enum class SynStyle {
    Normal,
    Keyword,
    String,
    Comment,
    Number,
    Preproc,
};

struct SynState {
    bool in_block_comment = false;
    bool in_multiline_str = false;
    char ml_str_char      = '"';
};

struct SynRun {
    std::size_t start;
    std::size_t len;
    SynStyle    style;
};

// Detect language from file path / name (extension-based).
SynLang syn_detect(const std::string &filename);

// Tokenize one line, updating `state` for inter-line constructs.
// Runs are in byte-offset order, non-overlapping; gaps are Normal.
std::vector<SynRun> syn_parse_line(const std::string &line,
                                   SynState           &state,
                                   SynLang             lang);

#endif  // TUI_SYN_PARSE_H
