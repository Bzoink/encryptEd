// Unit tests for the incremental syntax rescan engine.
//
// The central property: after any edit, the incrementally updated style bytes
// must equal a fresh full scan of the edited document.

#include <cstdio>
#include <string>
#include <vector>

#include "highlight/rescan.h"
#include "highlight/syn_parse.h"

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

namespace {

constexpr unsigned char kNormal = static_cast<unsigned char>(SynStyle::Normal);

// Full scan through a throwaway Rescanner, for comparison.
std::vector<unsigned char> full_scan(const std::string &text, SynLang lang) {
    highlight::Rescanner r;
    r.set_language(lang);
    std::vector<unsigned char> styles;
    r.rescan_all(text, styles);
    return styles;
}

// Apply an edit the same way the GUI does: patch text and style bytes, then
// let the rescan engine recompute the affected range.
void edit(std::string &text, std::vector<unsigned char> &styles,
          highlight::Rescanner &r, std::size_t pos, std::size_t removed,
          const std::string &inserted) {
    text.replace(pos, removed, inserted);
    styles.erase(styles.begin() + static_cast<std::ptrdiff_t>(pos),
                 styles.begin() + static_cast<std::ptrdiff_t>(pos + removed));
    styles.insert(styles.begin() + static_cast<std::ptrdiff_t>(pos),
                  inserted.size(), kNormal);
    r.rescan_after_edit(text, pos, removed, inserted.size(), styles);
}

bool same(const std::vector<unsigned char> &a,
          const std::vector<unsigned char> &b) {
    return a == b;
}

std::string c_fixture(int lines) {
    std::string text;
    for (int i = 0; i < lines; ++i) {
        text += "int value_" + std::to_string(i) + " = " + std::to_string(i) +
                "; // line " + std::to_string(i) + "\n";
    }
    return text;
}

// Every edit case: incremental result must match a full rescan.
void check_equivalence(const char *name, SynLang lang, const std::string &base,
                       std::size_t pos, std::size_t removed,
                       const std::string &inserted) {
    std::string text = base;
    highlight::Rescanner r;
    r.set_language(lang);
    std::vector<unsigned char> styles;
    r.rescan_all(text, styles);
    CHECK(styles.size() == text.size());

    edit(text, styles, r, pos, removed, inserted);

    const std::vector<unsigned char> expected = full_scan(text, lang);
    if (!same(styles, expected)) {
        std::printf("FAIL %s: incremental styles differ from full scan\n", name);
        ++g_failures;
        for (std::size_t i = 0; i < styles.size() && i < expected.size(); ++i) {
            if (styles[i] != expected[i]) {
                std::printf("  first difference at byte %zu (%u != %u)\n", i,
                            styles[i], expected[i]);
                break;
            }
        }
    }
    CHECK(styles.size() == expected.size());
}

void test_open_block_comment() {
    const std::string base = c_fixture(200);
    // Insert an unterminated block comment: everything after it becomes a
    // comment, so the incremental scan cannot stop early.
    const std::size_t pos = base.find("int value_5");
    check_equivalence("open block comment", SynLang::C, base, pos, 0, "/* ");
}

void test_close_block_comment() {
    std::string base = c_fixture(200);
    const std::size_t open = base.find("int value_5");
    base.insert(open, "/* open\n");
    // Close it on a later line.
    const std::size_t close = base.find("int value_120");
    check_equivalence("close block comment", SynLang::C, base, close, 0, "*/\n");
}

void test_insert_newline() {
    const std::string base = c_fixture(200);
    const std::size_t pos = base.find("value_80 = 80");
    check_equivalence("insert newline", SynLang::C, base, pos, 0, "\n");
}

void test_delete_newline() {
    const std::string base = c_fixture(200);
    const std::size_t pos = base.find("value_90 = 90");
    const std::size_t nl = base.find('\n', pos);
    check_equivalence("delete newline", SynLang::C, base, nl, 1, " ");
}

void test_delete_span() {
    const std::string base = c_fixture(200);
    const std::size_t a = base.find("int value_60");
    const std::size_t b = base.find("int value_130");
    check_equivalence("delete span", SynLang::C, base, a, b - a, "");
}

void test_same_length_edit() {
    const std::string base = c_fixture(200);
    const std::size_t pos = base.find("value_42");
    check_equivalence("same-length edit", SynLang::C, base, pos, 8, "renamed_");
}

void test_edit_at_start() {
    const std::string base = c_fixture(200);
    check_equivalence("edit at start", SynLang::C, base, 0, 0, "/* head */\n");
}

void test_edit_at_end() {
    const std::string base = c_fixture(200);
    check_equivalence("edit at end", SynLang::C, base, base.size(), 0,
                      "int tail = 1;\n");
}

void test_python_triple_quote() {
    std::string base;
    for (int i = 0; i < 150; ++i)
        base += "x_" + std::to_string(i) + " = " + std::to_string(i) + "\n";
    const std::size_t pos = base.find("x_10 ");
    check_equivalence("python triple quote", SynLang::Python, base, pos, 0,
                      "s = \"\"\"\n");
}

void test_json_strings() {
    std::string base;
    for (int i = 0; i < 150; ++i)
        base += "  \"key_" + std::to_string(i) + "\": " + std::to_string(i) +
                ",\n";
    const std::size_t pos = base.find("\"key_70\"");
    check_equivalence("json string edit", SynLang::JSON, base, pos, 0, "\"");
}

// A local edit in the middle of a large document must stop before the end.
void test_early_stop() {
    std::string text = c_fixture(400);
    highlight::Rescanner r;
    r.set_language(SynLang::C);
    std::vector<unsigned char> styles;
    r.rescan_all(text, styles);

    const std::size_t pos = text.find("value_100");
    const auto dirty =
        r.rescan_after_edit(text, pos, 7, 7, styles);  // same length
    CHECK(dirty.start < dirty.end);
    CHECK(dirty.end < text.size());
    CHECK(dirty.start <= pos);
    CHECK(dirty.end > pos + 7);
}

// Checkpoints must stay sorted and on line starts after edits.
void test_checkpoints_valid() {
    std::string text = c_fixture(400);
    highlight::Rescanner r;
    r.set_language(SynLang::C);
    std::vector<unsigned char> styles;
    r.rescan_all(text, styles);

    edit(text, styles, r, text.find("value_30"), 0, "\n");
    edit(text, styles, r, text.find("value_200"), 4, "");

    const auto &cps = r.checkpoints();
    CHECK(!cps.empty());
    for (std::size_t i = 1; i < cps.size(); ++i) {
        CHECK(cps[i].offset > cps[i - 1].offset);
        CHECK(cps[i].offset <= text.size());
        CHECK(cps[i].offset == 0 || text[cps[i].offset - 1] == '\n');
    }
}

// Bytes before the reported dirty range must be untouched.
void test_dirty_range_boundaries() {
    std::string text = c_fixture(400);
    highlight::Rescanner r;
    r.set_language(SynLang::C);
    std::vector<unsigned char> styles;
    r.rescan_all(text, styles);

    std::vector<unsigned char> before = styles;
    const std::size_t pos = text.find("value_150");
    const auto dirty = r.rescan_after_edit(text, pos, 0, 0, styles);
    for (std::size_t i = 0; i < dirty.start && i < before.size(); ++i)
        CHECK(styles[i] == before[i]);
}

// Language switches reset cached state.
void test_language_switch() {
    highlight::Rescanner r;
    r.set_language(SynLang::C);
    std::vector<unsigned char> styles;
    std::string text = "int x; // c\n";
    r.rescan_all(text, styles);
    CHECK(!r.checkpoints().empty());

    r.set_language(SynLang::Python);
    CHECK(r.checkpoints().empty());
    CHECK(r.language() == SynLang::Python);
}

}  // namespace

int main() {
    test_open_block_comment();
    test_close_block_comment();
    test_insert_newline();
    test_delete_newline();
    test_delete_span();
    test_same_length_edit();
    test_edit_at_start();
    test_edit_at_end();
    test_python_triple_quote();
    test_json_strings();
    test_early_stop();
    test_checkpoints_valid();
    test_dirty_range_boundaries();
    test_language_switch();

    if (g_failures == 0) {
        std::printf("all rescan tests passed\n");
        return 0;
    }
    std::printf("%d rescan test(s) failed\n", g_failures);
    return 1;
}
