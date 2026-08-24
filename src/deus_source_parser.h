#ifndef DEUS_SOURCE_PARSER_H
#define DEUS_SOURCE_PARSER_H

#include "deus_source_ast.h"

int deus_source_is_modern(const char *source, size_t length);
int deus_source_parse_modern(const char *source, size_t length,
                             DeusSourceAst *out,
                             DeusDiagnostic *diagnostic);

#endif
