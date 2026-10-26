#ifndef ENCRYPTED_HIGHLIGHT_SYN_PARSE_H
#define ENCRYPTED_HIGHLIGHT_SYN_PARSE_H

// Syntax tokenizer shared by the terminal editor and the FLTK GUI.
//
// syn_parse_line() fills a list of non-overlapping byte-range style runs for
// one line, updating SynState for multi-line constructs (block comments,
// Python triple-quoted strings). The caller walks lines top-to-bottom
// accumulating state, so inter-line context is O(N) per frame total.
//
// The tokenizer has no UI dependency and is unit-tested standalone.

#include <cstddef>
#include <string>
#include <string_view>
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

    bool operator==(const SynState &o) const {
        return in_block_comment == o.in_block_comment &&
               in_multiline_str == o.in_multiline_str &&
               (!in_multiline_str || ml_str_char == o.ml_str_char);
    }
    bool operator!=(const SynState &o) const { return !(*this == o); }
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
// `runs` is cleared first and reused across calls to avoid per-line
// allocation when rescanning a whole document.
void syn_parse_line(std::string_view     line,
                    SynState            &state,
                    SynLang              lang,
                    std::vector<SynRun> &runs);

#endif  // ENCRYPTED_HIGHLIGHT_SYN_PARSE_H
