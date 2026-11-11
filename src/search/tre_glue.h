#ifndef TXX_TRE_GLUE_H
#define TXX_TRE_GLUE_H

#include "tre.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Properties of a compiled pattern that TRE does not expose publicly. */
typedef struct {
    int states;     /* automaton size; match cost grows with it */
    int backrefs;   /* uses back-references (backtracking matcher) */
    int approx;     /* uses approximate matching {~n} */
} txx_tre_info;

void txx_tre_get_info(const regex_t *compiled, txx_tre_info *out);

#ifdef __cplusplus
}
#endif
#endif
