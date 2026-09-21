#include "deus_semantic_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==================== Utilitários de Tipo ==================== */

const char *deus_semantic_type_kind_name(DeusTypeKind kind) {
    switch (kind) {
        case DEUS_TYPE_KIND_NULL: return "Null";
        case DEUS_TYPE_KIND_BOOL: return "Bool";
        case DEUS_TYPE_KIND_I64: return "I64";
        case DEUS_TYPE_KIND_STRING: return "String";
        case DEUS_TYPE_KIND_DOCUMENT: return "Document";
        case DEUS_TYPE_KIND_FUTURE: return "Future";
        case DEUS_TYPE_KIND_ERROR: return "Error";
        case DEUS_TYPE_KIND_LIST: return "List";
        case DEUS_TYPE_KIND_RECORD: return "Record";
        case DEUS_TYPE_KIND_OPTIONAL: return "Optional";
        case DEUS_TYPE_KIND_DYNAMIC: return "Dynamic";
        default: return "Unknown";
    }
}

/* Verifica subtyping com refinamento limitado */
int deus_semantic_type_check_subtyping(DeusType *sub, DeusType *super) {
    if (!sub || !super) return 0;
    if (deus_type_equals(sub, super)) return 1;
    
    /* Dynamic é supertipo de tudo exceto Null */
    if (super->kind == DEUS_TYPE_KIND_DYNAMIC) {
        return sub->kind != DEUS_TYPE_KIND_NULL;
    }
    
    /* Null é subtipo de qualquer Optional<T> */
    if (sub->kind == DEUS_TYPE_KIND_NULL && super->kind == DEUS_TYPE_KIND_OPTIONAL) {
        return 1;
    }
    
    /* T é subtipo de T? (Optional<T>) */
    if (super->kind == DEUS_TYPE_KIND_OPTIONAL && sub->kind != DEUS_TYPE_KIND_NULL) {
        return deus_type_equals(sub, super->inner_type);
    }
    
    /* List<T> é covariante em T */
    if (sub->kind == DEUS_TYPE_KIND_LIST && super->kind == DEUS_TYPE_KIND_LIST) {
        return deus_semantic_type_check_subtyping(sub->element_type, super->element_type);
    }
    
    /* Future<T> é covariante em T */
    if (sub->kind == DEUS_TYPE_KIND_FUTURE && super->kind == DEUS_TYPE_KIND_FUTURE) {
        return deus_semantic_type_check_subtyping(sub->inner_type, super->inner_type);
    }
    
    /* Optional<T> é covariante em T */
    if (sub->kind == DEUS_TYPE_KIND_OPTIONAL && super->kind == DEUS_TYPE_KIND_OPTIONAL) {
        return deus_semantic_type_check_subtyping(sub->inner_type, super->inner_type);
    }
    
    /* Record: structural subtyping - todos os campos de super devem existir em sub */
    if (sub->kind == DEUS_TYPE_KIND_RECORD && super->kind == DEUS_TYPE_KIND_RECORD) {
        for (uint32_t i = 0; i < super->field_count; i++) {
            int found = 0;
            for (uint32_t j = 0; j < sub->field_count; j++) {
                if (sub->fields[j].name_length == super->fields[i].name_length &&
                    memcmp(sub->fields[j].name, super->fields[i].name, sub->fields[j].name_length) == 0) {
                    if (!deus_semantic_type_check_subtyping(sub->fields[j].type, super->fields[i].type)) {
                        return 0;
                    }
                    found = 1;
                    break;
                }
            }
            if (!found) return 0; /* Campo faltando em sub */
        }
        return 1;
    }
    
    return 0;
}

/* ==================== Gerenciamento de Escopo ==================== */

static DeusSemanticScope *scope_new(DeusSemanticScope *parent) {
    DeusSemanticScope *scope = (DeusSemanticScope *)calloc(1, sizeof(DeusSemanticScope));
    if (!scope) return NULL;
    scope->parent = parent;
    scope->symbols = NULL;
    scope->symbol_count = 0;
    scope->capacity = 0;
    return scope;
}

static void scope_free(DeusSemanticScope *scope) {
    if (!scope) return;
    for (uint32_t i = 0; i < scope->symbol_count; i++) {
        free(scope->symbols[i].name);
        if (scope->symbols[i].type.type) {
            /* Não libera o tipo aqui - tipos canônicos são compartilhados */
        }
    }
    free(scope->symbols);
    free(scope);
}

DeusSemanticScope *deus_semantic_scope_push(DeusSemanticContext *ctx) {
    DeusSemanticScope *new_scope = scope_new(ctx->current_scope);
    if (!new_scope) return NULL;
    ctx->current_scope = new_scope;
    return new_scope;
}

void deus_semantic_scope_pop(DeusSemanticContext *ctx) {
    if (!ctx->current_scope) return;
    DeusSemanticScope *to_free = ctx->current_scope;
    ctx->current_scope = to_free->parent;
    scope_free(to_free);
}

/* ==================== Operações de Símbolo ==================== */

int deus_semantic_declare_symbol(DeusSemanticContext *ctx, const char *name,
                                  uint32_t name_length, uint32_t slot,
                                  DeusType *type, unsigned line, unsigned column) {
    if (!ctx || !ctx->current_scope || !name) return 0;
    
    /* Verifica se já existe no escopo atual */
    for (uint32_t i = 0; i < ctx->current_scope->symbol_count; i++) {
        if (ctx->current_scope->symbols[i].name_length == name_length &&
            memcmp(ctx->current_scope->symbols[i].name, name, name_length) == 0) {
            /* Símbolo já declarado neste escopo */
            if (ctx->diagnostic) {
                ctx->diagnostic->line = line;
                ctx->diagnostic->column = column;
                snprintf(ctx->diagnostic->message, sizeof(ctx->diagnostic->message),
                         "symbol '%.*s' already declared", name_length, name);
            }
            return 0;
        }
    }
    
    /* Expande array se necessário */
    if (ctx->current_scope->symbol_count >= ctx->current_scope->capacity) {
        uint32_t new_capacity = ctx->current_scope->capacity == 0 ? 16 : ctx->current_scope->capacity * 2;
        DeusSemanticSymbol *new_symbols = (DeusSemanticSymbol *)realloc(
            ctx->current_scope->symbols, new_capacity * sizeof(DeusSemanticSymbol));
        if (!new_symbols) return 0;
        ctx->current_scope->symbols = new_symbols;
        ctx->current_scope->capacity = new_capacity;
    }
    
    /* Adiciona símbolo */
    DeusSemanticSymbol *sym = &ctx->current_scope->symbols[ctx->current_scope->symbol_count++];
    memset(sym, 0, sizeof(*sym));
    
    sym->name = (char *)malloc(name_length + 1);
    if (!sym->name) return 0;
    memcpy(sym->name, name, name_length);
    sym->name[name_length] = '\0';
    sym->name_length = name_length;
    sym->slot = slot;
    sym->type.type = type;
    sym->type.is_constant = 0;
    sym->type.is_readonly = 0;
    sym->type.capabilities = 0;
    sym->is_initialized = 0;
    sym->line = line;
    sym->column = column;
    
    return 1;
}

DeusSemanticSymbol *deus_semantic_resolve_symbol(DeusSemanticContext *ctx,
                                                  const char *name, uint32_t name_length) {
    if (!ctx || !name) return NULL;
    
    /* Busca do escopo atual até o global */
    DeusSemanticScope *scope = ctx->current_scope;
    while (scope) {
        for (uint32_t i = 0; i < scope->symbol_count; i++) {
            if (scope->symbols[i].name_length == name_length &&
                memcmp(scope->symbols[i].name, name, name_length) == 0) {
                return &scope->symbols[i];
            }
        }
        scope = scope->parent;
    }
    
    return NULL; /* Não encontrado */
}

/* ==================== Inferência de Tipo ==================== */

static DeusSemanticType infer_literal_type(const DeusExpressionNode *expr) {
    DeusSemanticType result = {0};
    
    if (expr->literal_kind == DEUS_AST_EXPRESSION_NULL) {
        result.type = deus_type_null();
    } else if (expr->literal_kind == DEUS_AST_EXPRESSION_BOOL) {
        result.type = deus_type_bool();
        result.is_constant = 1;
    } else if (expr->literal_kind == DEUS_AST_EXPRESSION_I64) {
        result.type = deus_type_i64();
        result.is_constant = 1;
    } else if (expr->literal_kind == DEUS_AST_EXPRESSION_STRING) {
        result.type = deus_type_string();
        result.is_constant = 1;
    } else {
        result.type = deus_type_dynamic();
    }
    
    return result;
}

DeusSemanticType deus_semantic_type_infer_expression(DeusSemanticContext *ctx,
                                                      const DeusExpressionNode *expr) {
    DeusSemanticType result = {0};
    
    if (!ctx || !expr) {
        result.type = deus_type_dynamic();
        return result;
    }
    
    switch (expr->kind) {
        case DEUS_EXPRESSION_LITERAL:
            return infer_literal_type(expr);
        
        case DEUS_EXPRESSION_LOCAL: {
            DeusSemanticSymbol *sym = deus_semantic_resolve_symbol(
                ctx, expr->symbol, expr->symbol_length);
            if (sym) {
                result = sym->type;
            } else {
                result.type = deus_type_dynamic();
                if (ctx->diagnostic) {
                    ctx->diagnostic->line = expr->line;
                    ctx->diagnostic->column = expr->column;
                    snprintf(ctx->diagnostic->message, sizeof(ctx->diagnostic->message),
                             "unknown symbol '%.*s'", expr->symbol_length, expr->symbol);
                }
            }
            break;
        }
        
        case DEUS_EXPRESSION_UNARY: {
            DeusSemanticType operand_type = deus_semantic_type_infer_expression(ctx, expr->left);
            if (expr->operator_kind == DEUS_EXPRESSION_OP_NOT) {
                /* not requer Bool */
                if (operand_type.type->kind != DEUS_TYPE_KIND_BOOL &&
                    operand_type.type->kind != DEUS_TYPE_KIND_DYNAMIC) {
                    if (ctx->diagnostic) {
                        ctx->diagnostic->line = expr->line;
                        ctx->diagnostic->column = expr->column;
                        snprintf(ctx->diagnostic->message, sizeof(ctx->diagnostic->message),
                                 "unary 'not' requires Bool operand");
                    }
                }
                result.type = deus_type_bool();
            }
            break;
        }
        
        case DEUS_EXPRESSION_BINARY: {
            DeusSemanticType left_type = deus_semantic_type_infer_expression(ctx, expr->left);
            DeusSemanticType right_type = deus_semantic_type_infer_expression(ctx, expr->right);
            
            switch (expr->operator_kind) {
                case DEUS_EXPRESSION_OP_AND:
                case DEUS_EXPRESSION_OP_OR:
                    /* Operadores booleanos */
                    result.type = deus_type_bool();
                    break;
                    
                case DEUS_EXPRESSION_OP_EQUAL:
                case DEUS_EXPRESSION_OP_NOT_EQUAL:
                case DEUS_EXPRESSION_OP_LESS:
                case DEUS_EXPRESSION_OP_LESS_EQUAL:
                case DEUS_EXPRESSION_OP_GREATER:
                case DEUS_EXPRESSION_OP_GREATER_EQUAL:
                    /* Comparações retornam Bool */
                    result.type = deus_type_bool();
                    break;
                    
                case DEUS_EXPRESSION_OP_COALESCE:
                    /* a ?? b: tipo é Optional<T> ou T */
                    if (left_type.type->kind == DEUS_TYPE_KIND_OPTIONAL) {
                        result.type = deus_type_optional(left_type.type->inner_type);
                    } else if (left_type.type->kind == DEUS_TYPE_KIND_NULL) {
                        result.type = right_type.type;
                    } else {
                        result.type = left_type.type;
                    }
                    break;
                    
                default:
                    result.type = deus_type_dynamic();
                    break;
            }
            break;
        }
        
        case DEUS_EXPRESSION_CONVERSION: {
            /* Inferir tipo da fonte para validação (opcional) */
            deus_semantic_type_infer_expression(ctx, expr->left);
            
            switch (expr->operator_kind) {
                case DEUS_EXPRESSION_OP_TEXT:
                    result.type = deus_type_string();
                    break;
                case DEUS_EXPRESSION_OP_I64:
                    result.type = deus_type_i64();
                    break;
                case DEUS_EXPRESSION_OP_BOOL:
                    result.type = deus_type_bool();
                    break;
                default:
                    result.type = deus_type_dynamic();
                    break;
            }
            break;
        }
        
        default:
            result.type = deus_type_dynamic();
            break;
    }
    
    return result;
}

/* ==================== Checagem de Atribuição ==================== */

int deus_semantic_type_check_assignment(DeusSemanticContext *ctx, DeusType *target,
                                         DeusType *source, unsigned line, unsigned column) {
    if (!target || !source) {
        if (ctx && ctx->diagnostic) {
            snprintf(ctx->diagnostic->message, sizeof(ctx->diagnostic->message),
                     "invalid type in assignment");
            ctx->diagnostic->line = line;
            ctx->diagnostic->column = column;
        }
        return 0;
    }
    
    /* Tipos iguais */
    if (deus_type_equals(target, source)) {
        return 1;
    }
    
    /* Subtyping */
    if (deus_semantic_type_check_subtyping(source, target)) {
        return 1;
    }
    
    /* Dynamic pode ser atribuído a qualquer tipo (com warning em modo strict) */
    if (source->kind == DEUS_TYPE_KIND_DYNAMIC) {
        return 1;
    }
    
    /* Falha na checagem */
    if (ctx && ctx->diagnostic) {
        char target_str[64], source_str[64];
        deus_type_to_string(target, target_str, sizeof(target_str));
        deus_type_to_string(source, source_str, sizeof(source_str));
        
        snprintf(ctx->diagnostic->message, sizeof(ctx->diagnostic->message),
                 "type mismatch");
        ctx->diagnostic->line = line;
        ctx->diagnostic->column = column;
    }
    
    return 0;
}

/* ==================== Contexto Semântico ==================== */

int deus_semantic_context_init(DeusSemanticContext *ctx, DeusDiagnostic *diagnostic,
                                const char *source, size_t length) {
    if (!ctx) return 0;
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->diagnostic = diagnostic;
    ctx->source = source;
    ctx->source_length = length;
    
    /* Cria escopo global */
    ctx->global_scope = scope_new(NULL);
    if (!ctx->global_scope) return 0;
    
    ctx->current_scope = ctx->global_scope;
    
    return 1;
}

void deus_semantic_context_destroy(DeusSemanticContext *ctx) {
    if (!ctx) return;
    
    /* Pop todos os escopos até o global */
    while (ctx->current_scope && ctx->current_scope->parent) {
        deus_semantic_scope_pop(ctx);
    }
    
    /* Libera escopo global */
    if (ctx->global_scope) {
        scope_free(ctx->global_scope);
        ctx->global_scope = NULL;
    }
    
    ctx->current_scope = NULL;
}
