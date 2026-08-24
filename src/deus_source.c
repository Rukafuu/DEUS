#include "deus_compiler.h"
#include "deus_source_lower.h"
#include "deus_source_parser.h"

int deus_parse_source(const char *source, size_t length, DeusProgram *out,
                      DeusDiagnostic *diagnostic) {
    DeusAstProgram ast;
    DeusSourceAst source_ast;
    int result;
    if (deus_source_is_modern(source, length)) {
        if (!deus_source_parse_modern(source, length, &source_ast, diagnostic))
            return 0;
        if (!deus_source_lower(&source_ast, &ast, diagnostic)) {
            deus_source_ast_free(&source_ast);
            return 0;
        }
        deus_source_ast_free(&source_ast);
    } else if (!deus_parse_ast(source, length, &ast, diagnostic)) return 0;
    result = deus_analyze_and_generate(&ast, out, diagnostic);
    deus_ast_free(&ast);
    return result;
}
