/* Reads internals of a compiled TRE pattern. Built inside the TRE library
 * target so it sees the same configuration as TRE itself. */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "tre-internal.h"
#include "search/tre_glue.h"

void txx_tre_get_info(const regex_t *compiled, txx_tre_info *out)
{
    const tre_tnfa_t *tnfa = (const tre_tnfa_t *)compiled->TRE_REGEX_T_FIELD;
    out->states = tnfa->num_states;
    out->backrefs = tnfa->have_backrefs;
    out->approx = tnfa->have_approx;
}
