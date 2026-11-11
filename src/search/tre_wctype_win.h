#ifndef TXX_TRE_WCTYPE_WIN_H
#define TXX_TRE_WCTYPE_WIN_H

/* Windows only. The C runtime's iswalpha()/towlower() follow the process
 * locale and do not cover non-Latin text in the default "C" locale. These
 * replacements use the Win32 Unicode tables so [[:alpha:]], \w and
 * case-insensitive matching behave the same for every script. */
#include <wchar.h>
#include <wctype.h>

#ifdef __cplusplus
extern "C" {
#endif
int txx_iswalnum(wint_t c);
int txx_iswalpha(wint_t c);
int txx_iswblank(wint_t c);
int txx_iswcntrl(wint_t c);
int txx_iswdigit(wint_t c);
int txx_iswgraph(wint_t c);
int txx_iswlower(wint_t c);
int txx_iswprint(wint_t c);
int txx_iswpunct(wint_t c);
int txx_iswspace(wint_t c);
int txx_iswupper(wint_t c);
int txx_iswxdigit(wint_t c);
wint_t txx_towlower(wint_t c);
wint_t txx_towupper(wint_t c);
wctype_t txx_wctype(const char *name);
int txx_iswctype(wint_t c, wctype_t type);
#ifdef __cplusplus
}
#endif

#undef iswalnum
#define iswalnum  txx_iswalnum
#undef iswalpha
#define iswalpha  txx_iswalpha
#undef iswblank
#define iswblank  txx_iswblank
#undef iswcntrl
#define iswcntrl  txx_iswcntrl
#undef iswdigit
#define iswdigit  txx_iswdigit
#undef iswgraph
#define iswgraph  txx_iswgraph
#undef iswlower
#define iswlower  txx_iswlower
#undef iswprint
#define iswprint  txx_iswprint
#undef iswpunct
#define iswpunct  txx_iswpunct
#undef iswspace
#define iswspace  txx_iswspace
#undef iswupper
#define iswupper  txx_iswupper
#undef iswxdigit
#define iswxdigit txx_iswxdigit
#undef towlower
#define towlower  txx_towlower
#undef towupper
#define towupper  txx_towupper
#undef wctype
#define wctype    txx_wctype
#undef iswctype
#define iswctype  txx_iswctype

#endif
