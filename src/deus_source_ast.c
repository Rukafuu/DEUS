#include "deus_source_ast.h"

#include <stdlib.h>
#include <string.h>

void deus_source_ast_free(DeusSourceAst *ast) {
    size_t index;
    if (!ast) return;
    for (index = 0u; index < ast->flow.item_count; index++) {
        if (ast->flow.items[index].kind == DEUS_SOURCE_FLOW_LIMITS)
            free(ast->flow.items[index].as.limits.entries);
    }
    free(ast->flow.items);
    free(ast->flow.name);
    free(ast->events);
    free(ast->lines);
    free(ast->source);
    memset(ast, 0, sizeof(*ast));
}

const DeusSourceLogicalLine *deus_source_ast_lines(const DeusSourceAst *ast,
                                                   size_t *count) {
    if (count) *count = ast ? ast->line_count : 0u;
    return ast ? ast->lines : NULL;
}

const DeusSourceLayoutEvent *deus_source_ast_events(const DeusSourceAst *ast,
                                                    size_t *count) {
    if (count) *count = ast ? ast->event_count : 0u;
    return ast ? ast->events : NULL;
}
