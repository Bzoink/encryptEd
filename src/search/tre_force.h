/* Force-included into every vendored TRE translation unit (and the glue file):
 * routes TRE's heap use through the wiping, capped allocator (and, on
 * Windows, its character classification through Unicode-aware versions). System headers
 * that declare these functions are included first so only TRE's own calls
 * are redirected. */
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "search/tre_alloc.h"

#ifdef _WIN32
#include "search/tre_wctype_win.h"
#endif

#define malloc(size)          txx_tre_malloc(size)
#define calloc(count, size)   txx_tre_calloc((count), (size))
#define realloc(ptr, size)    txx_tre_realloc((ptr), (size))
#define free(ptr)             txx_tre_free(ptr)
