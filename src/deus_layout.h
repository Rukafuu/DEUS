#ifndef DEUS_LAYOUT_H
#define DEUS_LAYOUT_H

#include "deus.h"

int deus_layout_lower(const char *source, size_t length, char **output,
                      size_t *output_length, int *was_lowered,
                      DeusDiagnostic *diagnostic);

#endif
