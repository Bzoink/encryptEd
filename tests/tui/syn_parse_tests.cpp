// Unit tests for the etui syntax highlighter: language detection and runs.

#include <cstdio>
#include <string>
#include <vector>

#include "tui/syn_parse.h"

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

// Style covering `offset`, or Normal when no run covers it.
static SynStyle style_at(const std::vector<SynRun> &runs, std::size_t offset) {
    for (const SynRun &run : runs)
        if (offset >= run.start && offset < run.start + run.len) return run.style;
    return SynStyle::Normal;
}

// Style at the first occurrence of `needle`.
static SynStyle style_of(const std::vector<SynRun> &runs, const std::string &line,
                         const std::string &needle) {
    const std::size_t pos = line.find(needle);
    if (pos == std::string::npos) return SynStyle::Normal;
    return style_at(runs, pos);
}

// Style at the last byte of the first occurrence of `needle`.
static SynStyle style_last_of(const std::vector<SynRun> &runs, const std::string &line,
                              const std::string &needle) {
    const std::size_t pos = line.find(needle);
    if (pos == std::string::npos) return SynStyle::Normal;
    return style_at(runs, pos + needle.size() - 1);
}

static void test_detect() {
    CHECK(syn_detect("config.json") == SynLang::JSON);
    CHECK(syn_detect("/tmp/a/b/data.JSON") == SynLang::JSON);  // extension is case-insensitive
    CHECK(syn_detect("CMakeLists.txt") == SynLang::CMake);
    CHECK(syn_detect("main.cpp") == SynLang::Cpp);
    CHECK(syn_detect("script.sh") == SynLang::Shell);
    CHECK(syn_detect("notes.txt") == SynLang::None);
    CHECK(syn_detect("README") == SynLang::None);
    CHECK(syn_detect("archive.tar.gz") == SynLang::None);
}

static void test_json() {
    const std::string line =
        R"({"key": "value", "n": -12.5e3, "ok": true, "off": false, "nil": null})";
    SynState st{};
    const auto runs = syn_parse_line(line, st, SynLang::JSON);

    CHECK(style_of(runs, line, "\"key\"") == SynStyle::String);
    CHECK(style_of(runs, line, "\"value\"") == SynStyle::String);
    CHECK(style_of(runs, line, "12.5e3") == SynStyle::Number);
    CHECK(style_last_of(runs, line, "12.5e3") == SynStyle::Number);  // exponent included
    CHECK(style_of(runs, line, "true") == SynStyle::Keyword);
    CHECK(style_of(runs, line, "false") == SynStyle::Keyword);
    CHECK(style_of(runs, line, "null") == SynStyle::Keyword);
    CHECK(!st.in_block_comment && !st.in_multiline_str);

    // A URL inside a string must not start a comment.
    const std::string url = R"({"u": "http://example.com/x"})";
    SynState st2{};
    const auto runs2 = syn_parse_line(url, st2, SynLang::JSON);
    CHECK(style_of(runs2, url, "http://example.com/x") == SynStyle::String);

    // Literals inside strings stay strings.
    const std::string quoted = R"({"true": "null"})";
    SynState st3{};
    const auto runs3 = syn_parse_line(quoted, st3, SynLang::JSON);
    CHECK(style_of(runs3, quoted, "\"true\"") == SynStyle::String);
    CHECK(style_of(runs3, quoted, "\"null\"") == SynStyle::String);

    // Numbers keep sign-adjacent digits and exponents.
    const std::string nums = R"([1, 2.5, -3, 1E+6])";
    SynState st4{};
    const auto runs4 = syn_parse_line(nums, st4, SynLang::JSON);
    CHECK(style_of(runs4, nums, "2.5") == SynStyle::Number);
    CHECK(style_of(runs4, nums, "1E+6") == SynStyle::Number);
    CHECK(style_last_of(runs4, nums, "1E+6") == SynStyle::Number);
}

static void test_shared_paths() {
    const std::string py = "x = 1.5e-3  # note";
    SynState pst{};
    const auto pruns = syn_parse_line(py, pst, SynLang::Python);
    CHECK(style_of(pruns, py, "1.5e-3") == SynStyle::Number);
    CHECK(style_of(pruns, py, "# note") == SynStyle::Comment);

    const std::string open = "/* start";
    SynState cst{};
    const auto oruns = syn_parse_line(open, cst, SynLang::C);
    CHECK(cst.in_block_comment);
    CHECK(style_of(oruns, open, "/* start") == SynStyle::Comment);

    const std::string close = "still */ int x;";
    const auto cruns = syn_parse_line(close, cst, SynLang::C);
    CHECK(!cst.in_block_comment);
    CHECK(style_of(cruns, close, "int") == SynStyle::Keyword);
}

int main() {
    test_detect();
    test_json();
    test_shared_paths();

    if (g_failures == 0) {
        std::printf("all syntax highlighter tests passed\n");
        return 0;
    }
    std::printf("%d syntax highlighter test(s) failed\n", g_failures);
    return 1;
}
