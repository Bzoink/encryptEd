// Tests for the shared find/replace engine (plain text and TRE regex modes).
#include <chrono>
#include <cstdio>
#include <string>

#include "search/search.h"

using namespace search;

static int failures = 0;
#define CHECK(cond)                                                              \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// A freshly compiled query per check (Query is not copyable).
struct Q {
    Query q;
    bool ok;
    Q(const std::string &pattern, bool regex, bool match_case = true)
    {
        Options o; o.regex = regex; o.match_case = match_case;
        ok = q.compile(pattern, o);
    }
};

static std::string replace_all(const std::string &text, const std::string &pattern,
                               const std::string &replacement, bool regex,
                               bool match_case = true, std::size_t *count_out = nullptr)
{
    Q query(pattern, regex, match_case);
    std::string out; std::size_t count = 0;
    Result r = query.ok ? query.q.replace_all(text, replacement, out, count) : Result::Error;
    if (count_out) *count_out = count;
    return r == Result::Found ? out : text;
}

static bool find(const std::string &text, const std::string &pattern, bool regex, bool match_case,
                 std::size_t from, std::size_t &start, std::size_t &end)
{
    Q query(pattern, regex, match_case);
    Match m;
    if (!query.ok || query.q.find_next(text, from, m) != Result::Found) return false;
    start = m.whole.start; end = m.whole.end;
    return true;
}

static void plain_tests()
{
    std::size_t s, e;
    CHECK(find("hello world", "world", false, true, 0, s, e) && s == 6 && e == 11);
    CHECK(!find("hello world", "World", false, true, 0, s, e));
    CHECK(find("hello world", "World", false, false, 0, s, e) && s == 6 && e == 11);
    CHECK(find("héllo ÉCOLE", "école", false, false, 0, s, e) && s == 7 && e == 13);   // Unicode case folding
    CHECK(find("abc a.c", "a.c", false, true, 0, s, e) && s == 4);                       // '.' is literal in plain mode
    CHECK(find("x(y)z", "(y)", false, true, 0, s, e) && s == 1 && e == 4);
    CHECK(find("x(y)z", "(Y)", false, false, 0, s, e) && s == 1);                        // literal + ignore case

    Q prev("a", false);
    Match m;
    CHECK(prev.q.find_prev("banana", 6, m) == Result::Found && m.whole.start == 5);
    CHECK(prev.q.find_prev("banana", 5, m) == Result::Found && m.whole.start == 3);
    CHECK(prev.q.find_prev("banana", 1, m) == Result::NotFound);
    Q prev_i("A", false, false);
    CHECK(prev_i.q.find_prev("banana", 5, m) == Result::Found && m.whole.start == 3);

    std::size_t n = 0;
    CHECK(replace_all("a-a-a", "a", "bb", false, true, &n) == "bb-bb-bb" && n == 3);
    CHECK(replace_all("a-A-a", "a", "x", false, false, &n) == "x-x-x" && n == 3);
    CHECK(replace_all("a.b", ".", "\\1&", false, true, &n) == "a\\1&b" && n == 1);        // replacement literal
    CHECK(replace_all("aaa", "aa", "b", false, true, &n) == "ba" && n == 1);              // non-overlapping
    CHECK(replace_all("abc", "x", "y", false, true, &n) == "abc" && n == 0);
    CHECK(replace_all("abc", "b", "", false, true, &n) == "ac" && n == 1);                // delete

    Q empty("", false);
    CHECK(!empty.ok && !empty.q.error().empty());
}

static void regex_basic_tests()
{
    std::size_t s, e;
    CHECK(find("foo bar", "b.r", true, true, 0, s, e) && s == 4 && e == 7);
    CHECK(!find("foo bar", "B.R", true, true, 0, s, e));
    CHECK(find("foo bar", "B.R", true, false, 0, s, e) && s == 4);
    CHECK(find("cat dog", "dog|cat", true, true, 0, s, e) && s == 0 && e == 3);           // alternation
    CHECK(find("aaa", "a{2}", true, true, 0, s, e) && s == 0 && e == 2);
    CHECK(find("a1b22c", "[0-9]+", true, true, 0, s, e) && s == 1 && e == 2);
    CHECK(find("a1b22c", "\\d+", true, true, 2, s, e) && s == 3 && e == 5);
    CHECK(find("x  y", "\\s+", true, true, 0, s, e) && s == 1 && e == 3);
    CHECK(find("ab abc", "ab?c", true, true, 0, s, e) && s == 3);
    CHECK(find("<a><b>", "<.*?>", true, true, 0, s, e) && s == 0 && e == 3);              // lazy
    CHECK(find("first\nsecond", "^second$", true, true, 0, s, e) && s == 6 && e == 12);   // per-line anchors
    CHECK(!find("a\nb", "a.b", true, true, 0, s, e));                                     // '.' stops at newline
    CHECK(find("a\nb", "a\\nb", true, true, 0, s, e) && s == 0 && e == 3);

    Q invalid("a(b", true);
    CHECK(!invalid.ok && !invalid.q.error().empty());
    CHECK(invalid.q.error().find("a(b") == std::string::npos);   // never echoes the query
    Q bracket("[a", true);
    CHECK(!bracket.ok);
}

static void regex_context_tests()
{
    // Matching restarts mid-document; assertions must still see the text before.
    std::size_t n = 0;
    CHECK(replace_all("abab", "\\bab", "X", true, true, &n) == "Xab" && n == 1);          // not inside a word
    CHECK(replace_all("foo foo foo", "\\<foo\\>", "x", true, true, &n) == "x x x" && n == 3);
    CHECK(replace_all("a\nb\nc", "^", "> ", true, true, &n) == "> a\n> b\n> c" && n == 3); // empty matches applied
    CHECK(replace_all("a\nb\nc", "$", ";", true, true, &n) == "a;\nb;\nc;" && n == 3);
    CHECK(replace_all("one\ntwo", "^.", "#", true, true, &n) == "#ne\n#wo" && n == 2);
    CHECK(replace_all("aXa", "a", "b", true, true, &n) == "bXb");
    CHECK(replace_all("aaa", "^a", "b", true, true, &n) == "baa" && n == 1);              // '^' only at line start

    std::size_t s, e;
    CHECK(!find("foofoo", "\\bfoo", true, true, 3, s, e));                                 // find-next from mid-word
    CHECK(find("foo\nfoo", "^foo", true, true, 4, s, e) && s == 4);                        // after a newline
    CHECK(!find("xfoo", "^foo", true, true, 1, s, e));
}

static void regex_replace_tests()
{
    std::size_t n = 0;
    CHECK(replace_all("John Smith", "(\\w+) (\\w+)", "\\2, \\1", true, true, &n) == "Smith, John" && n == 1);
    CHECK(replace_all("a-b", "-", "\\n", true) == "a\nb");
    CHECK(replace_all("a-b", "-", "\\t", true) == "a\tb");
    CHECK(replace_all("a-b", "-", "\\\\", true) == "a\\b");
    CHECK(replace_all("a-b", "-", "\\.", true) == "a.b");                                   // unknown escape -> char
    CHECK(replace_all("abc", "b", "[\\0]", true) == "a[b]c");
    CHECK(replace_all("price 5", "(\\d)", "$\\1", true) == "price $5");                     // '$' is literal
    CHECK(replace_all("ab", "(x)?b", "<\\1>", true) == "a<>");                              // unmatched group is empty
    CHECK(replace_all("aXbXc", "x", "-", true, false) == "a-b-c");
    CHECK(replace_all("baaac", "a*", "X", true, true, &n) == "XbXcX" && n == 3);            // sed-style empties
    CHECK(replace_all("", "^", "x", true, true, &n) == "x" && n == 1);

    Q q("(a)(b)", true);
    CHECK(q.ok && q.q.group_count() == 2);
    CHECK(q.q.check_replacement("\\2\\1"));
    CHECK(!q.q.check_replacement("\\3"));
    Q plain("(a)", false);
    CHECK(plain.q.check_replacement("\\9"));                                                // literal in plain mode
}

static void unicode_tests()
{
    std::size_t s, e;
    CHECK(find("naïve café", "caf.", true, true, 0, s, e) && s == 7 && e == 12);            // '.' = one character
    CHECK(find("日本語のテキスト", "本.の", true, true, 0, s, e) && s == 3 && e == 12);
    CHECK(find("ÅÄÖ åäö", "åäö", true, false, 0, s, e) && s == 0 && e == 6);               // case-insensitive
    CHECK(find("ÅÄÖ åäö", "[[:alpha:]]+", true, true, 6, s, e) && s == 7 && e == 13);                  // POSIX class on non-ASCII
    CHECK(replace_all("café", "é", "e", true) == "cafe");
    CHECK(replace_all("日本日本", "日", "月", true) == "月本月本");
    CHECK(replace_all("a😀b😀", "😀", ":)", true) == "a:)b:)");
    CHECK(find("x😀y", "y", true, true, 0, s, e) && s == 5 && e == 6);                     // byte offsets after a 4-byte char
    // Invalid UTF-8 in the document must not crash or misreport offsets.
    const std::string bad = std::string("a\xff") + "b";
    CHECK(find(bad, "b", true, true, 0, s, e) && s == 2);
}

static void navigation_tests()
{
    Q q("[0-9]+", true);
    Match m;
    const std::string text = "a1 b22 c333";
    CHECK(q.q.find_next(text, 0, m) == Result::Found && m.whole.start == 1);
    CHECK(q.q.find_next(text, m.whole.end, m) == Result::Found && m.whole.start == 4 && m.whole.end == 6);
    CHECK(q.q.find_next(text, 6, m) == Result::Found && m.whole.start == 8);
    CHECK(q.q.find_next(text, 11, m) == Result::NotFound);
    CHECK(q.q.find_prev(text, text.size(), m) == Result::Found && m.whole.start == 8 && m.whole.end == 11);
    CHECK(q.q.find_prev(text, 8, m) == Result::Found && m.whole.start == 4);
    CHECK(q.q.find_prev(text, 4, m) == Result::Found && m.whole.start == 1);
    CHECK(q.q.find_prev(text, 1, m) == Result::NotFound);
    CHECK(q.q.match_exact(text, 4, 6, m) == Result::Found);
    CHECK(q.q.match_exact(text, 4, 5, m) == Result::NotFound);                              // shorter than the match
    Q e("x*", true);
    CHECK(e.q.find_next("abc", 0, m) == Result::NotFound);                                  // empty hits are skipped
    CHECK(e.q.find_next("abxxc", 0, m) == Result::Found && m.whole.start == 2 && m.whole.end == 4);
}

static void limit_tests()
{
    for (const char *bad : {"(a)\\1", "a{~1}b", "(((a{255}){255}){255}){255}", "a{300}", "a(b",
                            "(((((a{200}){200}){200}){200}){200})"}) {
        Q q(bad, true);
        CHECK(!q.ok);
        CHECK(!q.q.error().empty());
    }
    Q longp(std::string(5000, 'a'), true);
    CHECK(!longp.ok);
    // A pattern that explodes TRE's automaton fails cleanly and quickly.
    const auto start = std::chrono::steady_clock::now();
    Q blow("((((a{100}){100}){100}){100})", true);
    CHECK(!blow.ok);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    // The failure leaves the allocator usable.
    std::size_t n = 0;
    CHECK(replace_all("aaa", "a", "b", true, true, &n) == "bbb");
}

static void performance_tests()
{
    std::string big;
    for (int i = 0; i < 200000; ++i) big += "line " + std::to_string(i) + " some words here\n";   // ~5 MB
    auto t0 = std::chrono::steady_clock::now();
    std::size_t n = 0;
    std::string out = replace_all(big, "([a-z]+) ([0-9]+)", "\\2 \\1", true, true, &n);
    CHECK(n == 200000 && out.compare(0, 8, "0 line s") == 0);
    // Catastrophic-looking patterns stay linear (no backtracking engine).
    Q q("(a*)*b", true);
    const std::string as(100000, 'a');
    Match m;
    CHECK(q.ok && q.q.find_next(as, 0, m) == Result::NotFound);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(elapsed < std::chrono::seconds(60));
    std::printf("search performance: %.2fs\n", std::chrono::duration<double>(elapsed).count());
}

int main()
{
    plain_tests();
    regex_basic_tests();
    regex_context_tests();
    regex_replace_tests();
    unicode_tests();
    navigation_tests();
    limit_tests();
    performance_tests();
    CHECK(std::string(regex_library_version()) == "0.9.0");
    if (failures == 0) std::puts("Search tests passed.");
    return failures == 0 ? 0 : 1;
}
