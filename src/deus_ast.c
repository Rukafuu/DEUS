#include "deus_compiler.h"

#include <stdlib.h>
#include <string.h>

void deus_ast_free(DeusAstProgram *program) {
    if (!program) return;
    for (uint32_t index = 0; index < program->count; index++) {
        if (program->instructions[index].operand_kind == DEUS_AST_OPERAND_STRING)
            free(program->instructions[index].operand.string);
        if (program->instructions[index].symbol)
            free(program->instructions[index].symbol);
        if (program->instructions[index].expression_symbol)
            free(program->instructions[index].expression_symbol);
        deus_expression_free(program->instructions[index].expression);
    }
    free(program->instructions);
    memset(program, 0, sizeof(*program));
}

void deus_expression_free(DeusExpressionNode *expression) {
    if (!expression) return;
    deus_expression_free(expression->left);
    deus_expression_free(expression->right);
    if (expression->kind == DEUS_EXPRESSION_LITERAL &&
        expression->literal_kind == DEUS_AST_EXPRESSION_STRING) free(expression->literal.string);
    free(expression->symbol);
    free(expression);
}
