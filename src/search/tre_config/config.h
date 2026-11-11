/* Build configuration for the vendored TRE regex library (replaces the
 * autotools-generated config.h). Only the facilities TRE actually uses are
 * enabled. Character classes ([[:alpha:]], \w, \d, \s ...) need the C
 * library's wctype()/iswctype(); without them TRE rejects every class. */
#ifndef TXX_TRE_CONFIG_H
#define TXX_TRE_CONFIG_H

#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1
#define HAVE_ISWBLANK 1
#define HAVE_ISWCTYPE 1
#define HAVE_WCTYPE 1
#define HAVE_MBRTOWC 1
#define HAVE_MBSTATE_T 1
/* Class names ([:alpha:] ...) are converted to narrow strings with one of these. */
#ifdef _WIN32
#define HAVE_WCSTOMBS 1
#else
#define HAVE_WCSRTOMBS 1
#endif

/* regex_t field that stores the compiled automaton. */
#define TRE_REGEX_T_FIELD value

#endif
