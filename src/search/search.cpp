#include "search/search.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <locale.h>

extern "C" {
#include "platform/secure_mem.h"
#include "search/tre_alloc.h"
#include "search/tre_glue.h"
}

namespace search {
namespace {

// Limits that keep a hostile or careless pattern from freezing the editor.
constexpr std::size_t kMaxPatternChars   = 2048;
constexpr int         kMaxStates         = 2048;
constexpr std::size_t kMaxRegexTextBytes = 32u * 1024u * 1024u;
constexpr double      kMaxWork           = 4e9;   // states * characters
constexpr std::size_t kMaxGroups         = 9;

// ---------------------------------------------------------------------------
// Wiping helpers
// ---------------------------------------------------------------------------
void wipe(std::string &s)
{
    if (!s.empty()) txx_secure_zero(s.data(), s.size());
}

// Append to `out`; when it must grow, move to a fresh buffer and wipe the old
// one so no stale plaintext stays in freed heap.
void append(std::string &out, const char *data, std::size_t size)
{
    if (size == 0) return;
    if (out.capacity() - out.size() < size) {
        std::string grown;
        grown.reserve(std::max(out.size() + size, out.capacity() * 2));
        grown.append(out);
        wipe(out);
        out.swap(grown);
        wipe(grown);
    }
    out.append(data, size);
}

void append(std::string &out, std::string_view text) { append(out, text.data(), text.size()); }

// ---------------------------------------------------------------------------
// UTF-8 <-> wide characters
// ---------------------------------------------------------------------------
// Decode one code point; invalid bytes become U+FFFD and consume one byte.
std::size_t decode(const unsigned char *s, std::size_t len, std::size_t i, char32_t &cp)
{
    const unsigned char c = s[i];
    if (c < 0x80) { cp = c; return 1; }
    std::size_t need = 0;
    char32_t value = 0, minimum = 0;
    if      ((c & 0xE0) == 0xC0) { need = 1; value = c & 0x1F; minimum = 0x80; }
    else if ((c & 0xF0) == 0xE0) { need = 2; value = c & 0x0F; minimum = 0x800; }
    else if ((c & 0xF8) == 0xF0) { need = 3; value = c & 0x07; minimum = 0x10000; }
    else { cp = 0xFFFD; return 1; }
    if (i + need >= len) { cp = 0xFFFD; return 1; }   // truncated sequence
    for (std::size_t k = 1; k <= need; ++k) {
        if ((s[i + k] & 0xC0) != 0x80) { cp = 0xFFFD; return 1; }
        value = (value << 6) | (s[i + k] & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        cp = 0xFFFD; return 1;
    }
    cp = value;
    return need + 1;
}

constexpr bool kWideIs16 = sizeof(wchar_t) == 2;

bool is_low_surrogate(wchar_t c) { return kWideIs16 && c >= 0xDC00 && c <= 0xDFFF; }

// UTF-8 text as wide characters plus a map back to byte offsets. The wide copy
// is plaintext, so it is wiped on destruction and never reallocated.
class Wide {
public:
    explicit Wide(std::string_view text)
    {
        const auto *s = reinterpret_cast<const unsigned char *>(text.data());
        bool ascii = true;
        std::size_t units = 0;
        for (std::size_t i = 0; i < text.size();) {
            char32_t cp = 0;
            const std::size_t used = decode(s, text.size(), i, cp);
            if (used != 1 || cp >= 0x80) ascii = false;
            units += (kWideIs16 && cp > 0xFFFF) ? 2 : 1;
            i += used;
        }
        ascii_ = ascii && units == text.size();
        n_ = units;
        chars_.assign(units + 1, L'\0');
        if (!ascii_) offsets_.assign(units + 1, 0);
        std::size_t at = 0;
        for (std::size_t i = 0; i < text.size();) {
            char32_t cp = 0;
            const std::size_t used = decode(s, text.size(), i, cp);
            if (kWideIs16 && cp > 0xFFFF) {
                const char32_t v = cp - 0x10000;
                if (!ascii_) { offsets_[at] = static_cast<std::uint32_t>(i); }
                chars_[at++] = static_cast<wchar_t>(0xD800 + (v >> 10));
                // The low half maps to the end of the character, so a match
                // ending between the halves still covers the whole character.
                if (!ascii_) { offsets_[at] = static_cast<std::uint32_t>(i + used); }
                chars_[at++] = static_cast<wchar_t>(0xDC00 + (v & 0x3FF));
            } else {
                if (!ascii_) offsets_[at] = static_cast<std::uint32_t>(i);
                chars_[at++] = static_cast<wchar_t>(cp);
            }
            i += used;
        }
        if (!ascii_) offsets_[n_] = static_cast<std::uint32_t>(text.size());
        size_ = text.size();
    }
    ~Wide()
    {
        txx_secure_zero(chars_.data(), chars_.size() * sizeof(wchar_t));
    }
    Wide(const Wide &) = delete;
    Wide &operator=(const Wide &) = delete;

    const wchar_t *data() const { return chars_.data(); }
    std::size_t units() const { return n_; }

    std::size_t byte_of(std::size_t unit) const
    {
        if (ascii_) return unit;
        return unit >= n_ ? size_ : offsets_[unit];
    }

    // First character boundary at or after `byte`.
    std::size_t unit_of(std::size_t byte) const
    {
        if (ascii_) return std::min(byte, n_);
        if (byte >= size_) return n_;
        std::size_t unit = static_cast<std::size_t>(
            std::lower_bound(offsets_.begin(), offsets_.begin() + static_cast<std::ptrdiff_t>(n_),
                             static_cast<std::uint32_t>(byte)) - offsets_.begin());
        while (unit < n_ && is_low_surrogate(chars_[unit])) ++unit;
        return unit;
    }

private:
    std::vector<wchar_t> chars_;
    std::vector<std::uint32_t> offsets_;
    std::size_t n_ = 0, size_ = 0;
    bool ascii_ = true;
};

// TRE's character classes and case folding follow the C library's LC_CTYPE.
// Switch this thread to a UTF-8 locale while matching so non-ASCII letters
// behave the same whatever locale the application started in.
class Utf8Ctype {
public:
    Utf8Ctype()
    {
#ifndef _WIN32
        if (locale_t loc = shared()) { previous_ = uselocale(loc); active_ = true; }
#endif
    }
    ~Utf8Ctype()
    {
#ifndef _WIN32
        if (active_) uselocale(previous_);
#endif
    }
    Utf8Ctype(const Utf8Ctype &) = delete;
    Utf8Ctype &operator=(const Utf8Ctype &) = delete;

private:
#ifndef _WIN32
    static locale_t shared()
    {
        static const locale_t loc = [] {
            for (const char *name : {"C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"})
                if (locale_t l = newlocale(LC_CTYPE_MASK, name, static_cast<locale_t>(nullptr))) return l;
            return static_cast<locale_t>(nullptr);
        }();
        return loc;
    }
    locale_t previous_ = nullptr;
    bool active_ = false;
#endif
};

} // namespace

// ---------------------------------------------------------------------------
// Query implementation
// ---------------------------------------------------------------------------
struct Query::Impl {
    Options options;
    bool ready = false;
    std::string pattern;          // plain, case-sensitive queries only
    std::string error;
    regex_t re{};                 // pattern as given
    regex_t ctx{};                // "(.|\n)(pattern)": match with one character of left context
    bool has_re = false, has_ctx = false;
    std::size_t nsub = 0;

    ~Impl() { reset(); }

    void reset()
    {
        if (has_re) tre_regfree(&re);
        if (has_ctx) tre_regfree(&ctx);
        has_re = has_ctx = false;
        wipe(pattern);
        pattern.clear();
        nsub = 0;
        ready = false;
    }

    bool uses_engine() const { return options.regex || !options.match_case; }

    std::string describe(int code, const regex_t *compiled)
    {
        char message[160] = {};
        tre_regerror(code, compiled, message, sizeof(message));
        return message[0] ? message : "Invalid search pattern";
    }

    bool compile(std::string_view query, const Options &opts)
    {
        reset();
        options = opts;
        error.clear();
        if (query.empty()) { error = "Enter search text"; return false; }
        if (!uses_engine()) {
            pattern.assign(query);
            ready = true;
            return true;
        }
        Wide wide(query);
        if (wide.units() > kMaxPatternChars) { error = "Search pattern is too long"; return false; }

        Utf8Ctype locale;
        int flags = opts.regex ? (REG_EXTENDED | REG_NEWLINE) : REG_LITERAL;
        if (!opts.match_case) flags |= REG_ICASE;
        int rc = tre_regwncomp(&re, wide.data(), wide.units(), flags);
        if (rc != REG_OK) { error = describe(rc, &re); return false; }
        has_re = true;
        nsub = re.re_nsub;

        txx_tre_info info{};
        txx_tre_get_info(&re, &info);
        if (info.backrefs) { error = "Back-references are not supported"; reset(); return false; }
        if (info.approx)   { error = "Approximate matching is not supported"; reset(); return false; }
        if (info.states > kMaxStates) { error = "Search pattern is too complex"; reset(); return false; }
        states = info.states;

        if (opts.regex) {
            // Matching restarts in the middle of the document after every hit.
            // Handing TRE one preceding character keeps ^, \b, \< and \> exact
            // there; the group wrapper makes that character part of the match.
            std::vector<wchar_t> wrapped;
            wrapped.reserve(wide.units() + 12);
            for (wchar_t c : std::wstring(L"(.|\n)(")) wrapped.push_back(c);
            wrapped.insert(wrapped.end(), wide.data(), wide.data() + wide.units());
            wrapped.push_back(L')');
            rc = tre_regwncomp(&ctx, wrapped.data(), wrapped.size(), flags);
            txx_secure_zero(wrapped.data(), wrapped.size() * sizeof(wchar_t));
            if (rc != REG_OK) { error = describe(rc, &ctx); reset(); return false; }
            has_ctx = true;
        }
        ready = true;
        return true;
    }

    int states = 0;

    // Match starting at or after wide index `p`.
    Result exec(const Wide &wide, std::size_t p, Match &match)
    {
        const std::size_t n = wide.units();
        if (p > n) return Result::NotFound;
        const std::size_t groups = std::min(nsub, kMaxGroups) + 1;
        regmatch_t slots[kMaxGroups + 3];
        std::size_t base = p, shift = 0;
        int rc;
        if (options.regex && p > 0) {
            base = p - 1;
            shift = 2;
            rc = tre_regwnexec(&ctx, wide.data() + base, n - base, groups + shift, slots, 0);
        } else {
            rc = tre_regwnexec(&re, wide.data() + base, n - base, groups, slots, 0);
        }
        if (rc == REG_NOMATCH) return Result::NotFound;
        if (rc != REG_OK) { error = describe(rc, &re); return Result::Error; }
        match.groups.assign(groups, Span{});
        for (std::size_t i = 0; i < groups; ++i) {
            const regmatch_t &r = slots[i + shift];
            if (r.rm_so < 0) continue;
            match.groups[i].start = wide.byte_of(base + static_cast<std::size_t>(r.rm_so));
            match.groups[i].end   = wide.byte_of(base + static_cast<std::size_t>(r.rm_eo));
        }
        match.whole = match.groups[0];
        if (!match.whole.matched()) return Result::NotFound;
        return Result::Found;
    }

    bool admit(std::string_view text)
    {
        if (text.size() > kMaxRegexTextBytes) {
            error = "Document is too large for this kind of search";
            return false;
        }
        if (static_cast<double>(states) * static_cast<double>(text.size()) > kMaxWork) {
            error = "Search pattern is too complex for this document";
            return false;
        }
        return true;
    }
};

Query::Query() : impl_(new Impl) {}
Query::~Query() = default;

bool Query::compile(std::string_view query, const Options &options) { return impl_->compile(query, options); }
bool Query::valid() const { return impl_->ready; }
const Options &Query::options() const { return impl_->options; }
const std::string &Query::error() const { return impl_->error; }
std::size_t Query::group_count() const { return impl_->nsub; }

bool Query::check_replacement(std::string_view replacement)
{
    Impl &q = *impl_;
    if (!q.options.regex) return true;
    for (std::size_t i = 0; i + 1 < replacement.size(); ++i) {
        if (replacement[i] != '\\') continue;
        const char d = replacement[++i];
        if (d >= '0' && d <= '9' && static_cast<std::size_t>(d - '0') > q.nsub) {
            q.error = "Replacement refers to a group the pattern does not have";
            return false;
        }
    }
    return true;
}

Result Query::find_next(std::string_view text, std::size_t from, Match &match)
{
    Impl &q = *impl_;
    if (!q.ready) return Result::Error;
    from = std::min(from, text.size());
    if (!q.uses_engine()) {
        const std::size_t at = text.find(q.pattern, from);
        if (at == std::string_view::npos) return Result::NotFound;
        match.groups.assign(1, Span{at, at + q.pattern.size()});
        match.whole = match.groups[0];
        return Result::Found;
    }
    if (!q.admit(text)) return Result::Error;
    Utf8Ctype locale;
    Wide wide(text);
    std::size_t p = wide.unit_of(from);
    while (true) {
        const Result r = q.exec(wide, p, match);
        if (r != Result::Found) return r;
        if (match.whole.end > match.whole.start) return Result::Found;
        p = std::max(p, wide.unit_of(match.whole.start)) + 1;   // skip empty hits
    }
}

Result Query::find_prev(std::string_view text, std::size_t before, Match &match)
{
    Impl &q = *impl_;
    if (!q.ready) return Result::Error;
    before = std::min(before, text.size());
    if (!q.uses_engine()) {
        if (before == 0 || q.pattern.size() > text.size()) return Result::NotFound;
        const std::size_t at = text.rfind(q.pattern, before - 1);
        if (at == std::string_view::npos) return Result::NotFound;
        match.groups.assign(1, Span{at, at + q.pattern.size()});
        match.whole = match.groups[0];
        return Result::Found;
    }
    if (!q.admit(text)) return Result::Error;
    Utf8Ctype locale;
    Wide wide(text);
    bool found = false;
    Match current, best;
    std::size_t p = 0;
    while (true) {
        const Result r = q.exec(wide, p, current);
        if (r == Result::Error) return r;
        if (r == Result::NotFound || current.whole.start >= before) break;
        // Walk the same non-overlapping sequence replace_all would.
        if (current.whole.end > current.whole.start) {
            best = current; found = true;
            p = wide.unit_of(current.whole.end);
        } else {
            p = std::max(p, wide.unit_of(current.whole.start)) + 1;
        }
    }
    if (!found) return Result::NotFound;
    match = best;
    return Result::Found;
}

Result Query::match_exact(std::string_view text, std::size_t start, std::size_t end, Match &match)
{
    Impl &q = *impl_;
    if (!q.ready) return Result::Error;
    if (start >= end || end > text.size()) return Result::NotFound;
    if (!q.uses_engine()) {
        if (text.substr(start, end - start) != q.pattern) return Result::NotFound;
        match.groups.assign(1, Span{start, end});
        match.whole = match.groups[0];
        return Result::Found;
    }
    if (!q.admit(text)) return Result::Error;
    Utf8Ctype locale;
    Wide wide(text);
    const Result r = q.exec(wide, wide.unit_of(start), match);
    if (r != Result::Found) return r;
    return (match.whole.start == start && match.whole.end == end) ? Result::Found : Result::NotFound;
}

void Query::expand(std::string_view text, const Match &match,
                   std::string_view replacement, std::string &out) const
{
    if (!impl_->options.regex) { append(out, replacement); return; }
    for (std::size_t i = 0; i < replacement.size(); ++i) {
        const char c = replacement[i];
        if (c != '\\' || i + 1 == replacement.size()) { append(out, &c, 1); continue; }
        const char d = replacement[++i];
        if (d >= '0' && d <= '9') {
            const std::size_t g = static_cast<std::size_t>(d - '0');
            if (g < match.groups.size() && match.groups[g].matched())
                append(out, text.substr(match.groups[g].start, match.groups[g].end - match.groups[g].start));
        } else if (d == 'n') { append(out, "\n", 1);
        } else if (d == 't') { append(out, "\t", 1);
        } else { append(out, &d, 1); }
    }
}

Result Query::replace_all(std::string_view text, std::string_view replacement,
                          std::string &out, std::size_t &count)
{
    Impl &q = *impl_;
    count = 0;
    out.clear();
    if (!q.ready) return Result::Error;

    if (!q.uses_engine()) {
        std::size_t position = 0, at = 0;
        while ((at = text.find(q.pattern, position)) != std::string_view::npos) {
            append(out, text.substr(position, at - position));
            append(out, replacement);
            position = at + q.pattern.size();
            ++count;
        }
        if (count == 0) return Result::NotFound;
        append(out, text.substr(position));
        return Result::Found;
    }

    if (!q.admit(text)) return Result::Error;
    Utf8Ctype locale;
    Wide wide(text);
    std::size_t copy_from = 0, last_end = npos, p = 0;
    Match m;
    out.reserve(text.size());
    while (p <= wide.units()) {
        const Result r = q.exec(wide, p, m);
        if (r == Result::NotFound) break;
        if (r == Result::Error) { wipe(out); out.clear(); count = 0; return r; }
        const std::size_t s = m.whole.start, e = m.whole.end;
        if (s == e && s == last_end) {      // empty hit right after a match
            p = wide.unit_of(s) + 1;
            continue;
        }
        append(out, text.substr(copy_from, s - copy_from));
        expand(text, m, replacement, out);
        copy_from = e;
        last_end = e;
        ++count;
        p = (e > s) ? wide.unit_of(e) : wide.unit_of(s) + 1;
    }
    if (count == 0) { wipe(out); out.clear(); return Result::NotFound; }
    append(out, text.substr(copy_from));
    return Result::Found;
}

const char *regex_library_version() { return TRE_VERSION; }

} // namespace search
