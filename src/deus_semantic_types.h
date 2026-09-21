#ifndef DEUS_SEMANTIC_TYPES_H
#define DEUS_SEMANTIC_TYPES_H

#include "deus_type.h"
#include "deus_compiler.h"
#include <stdint.h>
#include <stddef.h>

/* Tipo semântico resolvido - wrapper sobre DeusType com informações adicionais */
typedef struct DeusSemanticType {
    DeusType *type;              /* Tipo canônico resolvido */
    int is_constant;             /* Se o valor é conhecido em tempo de compilação */
    int is_readonly;             /* Se o valor é imutável */
    uint32_t capabilities;       /* Capabilities requeridas/inferidas */
} DeusSemanticType;

/* Símbolo na tabela de símbolos com tipo resolvido */
typedef struct {
    char *name;
    uint32_t name_length;
    uint32_t slot;               /* Slot na VM */
    DeusSemanticType type;
    int is_initialized;
    unsigned line, column;       /* Local de definição para diagnósticos */
} DeusSemanticSymbol;

/* Tabela de símbolos escopada */
typedef struct DeusSemanticScope {
    DeusSemanticSymbol *symbols;
    uint32_t symbol_count;
    uint32_t capacity;
    struct DeusSemanticScope *parent;
} DeusSemanticScope;

/* Contexto de análise semântica */
typedef struct {
    DeusSemanticScope *current_scope;
    DeusSemanticScope *global_scope;
    DeusDiagnostic *diagnostic;
    const char *source;
    size_t source_length;
} DeusSemanticContext;

/* Inicializa/destroi contexto semântico */
int deus_semantic_context_init(DeusSemanticContext *ctx, DeusDiagnostic *diagnostic,
                                const char *source, size_t length);
void deus_semantic_context_destroy(DeusSemanticContext *ctx);

/* Gerenciamento de escopo */
DeusSemanticScope *deus_semantic_scope_push(DeusSemanticContext *ctx);
void deus_semantic_scope_pop(DeusSemanticContext *ctx);

/* Operações de símbolo */
int deus_semantic_declare_symbol(DeusSemanticContext *ctx, const char *name,
                                  uint32_t name_length, uint32_t slot,
                                  DeusType *type, unsigned line, unsigned column);
DeusSemanticSymbol *deus_semantic_resolve_symbol(DeusSemanticContext *ctx,
                                                  const char *name, uint32_t name_length);

/* Operações de tipo */
DeusSemanticType deus_semantic_type_infer_expression(DeusSemanticContext *ctx,
                                                      const DeusExpressionNode *expr);
int deus_semantic_type_check_assignment(DeusSemanticContext *ctx, DeusType *target,
                                         DeusType *source, unsigned line, unsigned column);
int deus_semantic_type_check_subtyping(DeusType *sub, DeusType *super);

/* Utilitários */
const char *deus_semantic_type_kind_name(DeusTypeKind kind);

#endif
