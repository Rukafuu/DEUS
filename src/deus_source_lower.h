#ifndef DEUS_SOURCE_LOWER_H
#define DEUS_SOURCE_LOWER_H

#include "deus_compiler.h"
#include "deus_source_ast.h"

int deus_source_lower(const DeusSourceAst *source, DeusAstProgram *out,
                      DeusDiagnostic *diagnostic);

#endif
