#include <windows.h>
#include <string.h>

#include "search/tre_wctype_win.h"
#undef iswalnum
#undef iswalpha
#undef iswblank
#undef iswcntrl
#undef iswdigit
#undef iswgraph
#undef iswlower
#undef iswprint
#undef iswpunct
#undef iswspace
#undef iswupper
#undef iswxdigit
#undef towlower
#undef towupper
#undef wctype
#undef iswctype

/* Character type bits from the Unicode tables. wint_t is 16 bits here, so
 * the engine only ever sees UTF-16 code units; a surrogate half classifies as
 * nothing. */
static WORD kinds(wint_t c)
{
    WORD type = 0;
    wchar_t unit = (wchar_t)c;
    if (!GetStringTypeW(CT_CTYPE1, &unit, 1, &type)) return 0;
    return type;
}

int txx_iswalpha(wint_t c) { return (kinds(c) & C1_ALPHA) != 0; }
int txx_iswalnum(wint_t c) { return (kinds(c) & (C1_ALPHA | C1_DIGIT)) != 0; }
int txx_iswupper(wint_t c) { return (kinds(c) & C1_UPPER) != 0; }
int txx_iswlower(wint_t c) { return (kinds(c) & C1_LOWER) != 0; }
int txx_iswspace(wint_t c) { return (kinds(c) & C1_SPACE) != 0; }
int txx_iswblank(wint_t c) { return c == L' ' || c == L'\t' || (c > 0x7F && (kinds(c) & C1_BLANK) != 0); }
int txx_iswcntrl(wint_t c) { return (kinds(c) & C1_CNTRL) != 0; }
int txx_iswpunct(wint_t c) { return (kinds(c) & C1_PUNCT) != 0; }
int txx_iswdigit(wint_t c) { return c >= L'0' && c <= L'9'; }
int txx_iswxdigit(wint_t c) { return txx_iswdigit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'); }
int txx_iswgraph(wint_t c)
{
    const WORD k = kinds(c);
    return (k & (C1_ALPHA | C1_DIGIT | C1_PUNCT | C1_XDIGIT)) != 0 && !(k & (C1_SPACE | C1_CNTRL));
}
int txx_iswprint(wint_t c) { return c == L' ' || txx_iswgraph(c); }

static wint_t map(wint_t c, DWORD flag)
{
    wchar_t in = (wchar_t)c, out = 0;
    if (LCMapStringW(LOCALE_INVARIANT, flag, &in, 1, &out, 1) != 1) return c;
    return out;
}
wint_t txx_towlower(wint_t c) { return map(c, LCMAP_LOWERCASE); }
wint_t txx_towupper(wint_t c) { return map(c, LCMAP_UPPERCASE); }

static const struct { const char *name; int (*test)(wint_t); } classes[] = {
    {"alnum", txx_iswalnum}, {"alpha", txx_iswalpha}, {"blank", txx_iswblank},
    {"cntrl", txx_iswcntrl}, {"digit", txx_iswdigit}, {"graph", txx_iswgraph},
    {"lower", txx_iswlower}, {"print", txx_iswprint}, {"punct", txx_iswpunct},
    {"space", txx_iswspace}, {"upper", txx_iswupper}, {"xdigit", txx_iswxdigit},
};

/* A class handle is its 1-based index in the table. */
wctype_t txx_wctype(const char *name)
{
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; ++i)
        if (strcmp(name, classes[i].name) == 0) return (wctype_t)(i + 1);
    return 0;
}

int txx_iswctype(wint_t c, wctype_t type)
{
    if (type == 0 || type > sizeof classes / sizeof classes[0]) return 0;
    return classes[type - 1].test(c);
}
