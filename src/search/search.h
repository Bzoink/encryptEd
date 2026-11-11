#ifndef ENCRYPTED_SEARCH_H
#define ENCRYPTED_SEARCH_H

// Find / replace engine shared by the FLTK GUI and the terminal editor.
//
// Two modes behind one interface:
//   plain  - the query is literal text (default);
//   regex  - extended regular expressions via the vendored TRE library, with
//            capture groups, alternation, {n,m}, lazy quantifiers, \w \d \s \b
//            and POSIX classes. '^' and '$' match at every line, and '.' does
//            not match a line break.
// Either mode can be case sensitive or not.
//
// All offsets are UTF-8 byte offsets into the text the caller passes in, so
// callers never deal with wide characters. Temporary copies of the text made
// here are wiped before they are released.
//
// Behaviour worth knowing:
//   * find_next/find_prev skip zero-length matches; replace_all applies them
//     (so replacing '^' or '$' works) but never one directly after a match.
//   * Replacement text in regex mode understands \1..\9 (capture groups),
//     \0 (whole match), \n, \t and \\ ; any other \x yields x. In plain mode
//     the replacement is literal.
//   * Back-references and approximate matching are rejected, and patterns or
//     documents that would make matching unreasonably slow or large fail with
//     an error instead of freezing the editor.
//   * Not thread-safe.

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace search {

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

struct Options {
    bool regex = false;
    bool match_case = true;
};

struct Span {
    std::size_t start = npos;
    std::size_t end = npos;
    bool matched() const { return start != npos; }
};

struct Match {
    Span whole;
    // groups[0] is the whole match; groups[1..9] are capture groups (empty
    // Span when the group did not take part in the match).
    std::vector<Span> groups;
};

enum class Result { Found, NotFound, Error };

class Query {
public:
    Query();
    ~Query();
    Query(const Query &) = delete;
    Query &operator=(const Query &) = delete;

    // Prepare a query. Returns false and sets error() when the text is empty
    // or the pattern is invalid or too complex. The error message never
    // contains the query text.
    bool compile(std::string_view query, const Options &options);
    bool valid() const;
    const Options &options() const;
    const std::string &error() const;

    // Number of capture groups in the pattern (0 for plain queries).
    std::size_t group_count() const;

    // Verify a replacement against the query (regex mode: group references
    // must exist). Returns false and sets error() otherwise.
    bool check_replacement(std::string_view replacement);

    // First non-empty match starting at or after `from`.
    Result find_next(std::string_view text, std::size_t from, Match &match);
    // Last non-empty match starting before `before`.
    Result find_prev(std::string_view text, std::size_t before, Match &match);
    // Succeeds only when [start, end) is itself a match of the query; used
    // to decide whether "Replace" may act on the current selection.
    Result match_exact(std::string_view text, std::size_t start, std::size_t end, Match &match);

    // Append the replacement for `match` to `out` (expanding group
    // references in regex mode). `out` grows with its old buffer wiped.
    void expand(std::string_view text, const Match &match,
                std::string_view replacement, std::string &out) const;

    // Replace every match. `out` receives the whole new text; `count` the
    // number of replacements. With no match, count is 0 and out is empty.
    Result replace_all(std::string_view text, std::string_view replacement,
                       std::string &out, std::size_t &count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Version of the bundled regex library, e.g. "0.9.0".
const char *regex_library_version();

} // namespace search

#endif
