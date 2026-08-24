#ifndef DEUS_FORMATTER_H
#define DEUS_FORMATTER_H

#include "deus.h"

/*
 * Returns a newly allocated, NUL-terminated canonical representation.
 * The caller owns *output and must release it with free().
 */
int deus_format_source(const char *source, size_t length,
                       char **output, size_t *output_length,
                       DeusDiagnostic *diagnostic);

#endif
