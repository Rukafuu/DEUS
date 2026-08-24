#include "deus_compiler.h"
#include "deus_layout.h"

#include <stdlib.h>

int deus_parse_source(const char *source, size_t length, DeusProgram *out,
                      DeusDiagnostic *diagnostic) {
    DeusAstProgram ast;
    char *lowered = NULL; size_t lowered_length = 0u; int was_lowered = 0, result;
    if (!deus_layout_lower(source, length, &lowered, &lowered_length, &was_lowered, diagnostic)) return 0;
    if (was_lowered) { source = lowered; length = lowered_length; }
    if (!deus_parse_ast(source, length, &ast, diagnostic)) { free(lowered); return 0; }
    result = deus_analyze_and_generate(&ast, out, diagnostic);
    deus_ast_free(&ast);
    free(lowered);
    return result;
}
