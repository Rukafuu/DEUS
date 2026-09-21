#include "deus_type.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static DeusType *type_null = NULL;
static DeusType *type_bool = NULL;
static DeusType *type_i64 = NULL;
static DeusType *type_string = NULL;
static DeusType *type_document = NULL;
static DeusType *type_error = NULL;
static DeusType *type_dynamic = NULL;

static DeusType *new_type(DeusTypeKind kind) {
    DeusType *t = (DeusType *)calloc(1, sizeof(DeusType));
    if (!t) return NULL;
    t->kind = kind;
    t->element_type = NULL;
    t->inner_type = NULL;
    t->fields = NULL;
    t->field_count = 0;
    t->capabilities = 0;
    t->is_nullable = 0;
    return t;
}

DeusType *deus_type_null(void) {
    if (!type_null) {
        type_null = new_type(DEUS_TYPE_KIND_NULL);
    }
    return type_null;
}

DeusType *deus_type_bool(void) {
    if (!type_bool) {
        type_bool = new_type(DEUS_TYPE_KIND_BOOL);
    }
    return type_bool;
}

DeusType *deus_type_i64(void) {
    if (!type_i64) {
        type_i64 = new_type(DEUS_TYPE_KIND_I64);
    }
    return type_i64;
}

DeusType *deus_type_string(void) {
    if (!type_string) {
        type_string = new_type(DEUS_TYPE_KIND_STRING);
    }
    return type_string;
}

DeusType *deus_type_document(void) {
    if (!type_document) {
        type_document = new_type(DEUS_TYPE_KIND_DOCUMENT);
    }
    return type_document;
}

DeusType *deus_type_error(void) {
    if (!type_error) {
        type_error = new_type(DEUS_TYPE_KIND_ERROR);
    }
    return type_error;
}

DeusType *deus_type_dynamic(void) {
    if (!type_dynamic) {
        type_dynamic = new_type(DEUS_TYPE_KIND_DYNAMIC);
    }
    return type_dynamic;
}

DeusType *deus_type_future(DeusType *inner) {
    if (!inner) return NULL;
    DeusType *t = new_type(DEUS_TYPE_KIND_FUTURE);
    if (!t) return NULL;
    t->inner_type = inner;
    return t;
}

DeusType *deus_type_list(DeusType *element) {
    if (!element) return NULL;
    DeusType *t = new_type(DEUS_TYPE_KIND_LIST);
    if (!t) return NULL;
    t->element_type = element;
    return t;
}

DeusType *deus_type_optional(DeusType *inner) {
    if (!inner) return NULL;
    DeusType *t = new_type(DEUS_TYPE_KIND_OPTIONAL);
    if (!t) return NULL;
    t->inner_type = inner;
    t->is_nullable = 1;
    return t;
}

DeusType *deus_type_record(DeusField *fields, uint32_t count) {
    if (!fields && count > 0) return NULL;
    DeusType *t = new_type(DEUS_TYPE_KIND_RECORD);
    if (!t) return NULL;
    if (count > 0) {
        t->fields = (DeusField *)malloc(count * sizeof(DeusField));
        if (!t->fields) {
            free(t);
            return NULL;
        }
        memcpy(t->fields, fields, count * sizeof(DeusField));
        t->field_count = count;
    }
    return t;
}

void deus_type_free(DeusType *type) {
    if (!type) return;
    
    if (type == type_null || type == type_bool || type == type_i64 ||
        type == type_string || type == type_document || 
        type == type_error || type == type_dynamic) {
        return;
    }
    
    if (type->element_type) {
        deus_type_free(type->element_type);
    }
    if (type->inner_type) {
        deus_type_free(type->inner_type);
    }
    if (type->fields) {
        for (uint32_t i = 0; i < type->field_count; i++) {
            free(type->fields[i].name);
            deus_type_free(type->fields[i].type);
        }
        free(type->fields);
    }
    free(type);
}

int deus_type_equals(const DeusType *a, const DeusType *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    
    switch (a->kind) {
        case DEUS_TYPE_KIND_NULL:
        case DEUS_TYPE_KIND_BOOL:
        case DEUS_TYPE_KIND_I64:
        case DEUS_TYPE_KIND_STRING:
        case DEUS_TYPE_KIND_DOCUMENT:
        case DEUS_TYPE_KIND_ERROR:
        case DEUS_TYPE_KIND_DYNAMIC:
            return 1;
        
        case DEUS_TYPE_KIND_FUTURE:
        case DEUS_TYPE_KIND_OPTIONAL:
            return deus_type_equals(a->inner_type, b->inner_type);
        
        case DEUS_TYPE_KIND_LIST:
            return deus_type_equals(a->element_type, b->element_type);
        
        case DEUS_TYPE_KIND_RECORD: {
            if (a->field_count != b->field_count) return 0;
            for (uint32_t i = 0; i < a->field_count; i++) {
                if (a->fields[i].name_length != b->fields[i].name_length) return 0;
                if (memcmp(a->fields[i].name, b->fields[i].name, a->fields[i].name_length) != 0) return 0;
                if (!deus_type_equals(a->fields[i].type, b->fields[i].type)) return 0;
            }
            return 1;
        }
    }
    return 0;
}

int deus_type_is_subtype_of(const DeusType *sub, const DeusType *super) {
    if (!sub || !super) return 0;
    if (deus_type_equals(sub, super)) return 1;
    
    if (super->kind == DEUS_TYPE_KIND_DYNAMIC) {
        return sub->kind != DEUS_TYPE_KIND_NULL;
    }
    
    if (sub->kind == DEUS_TYPE_KIND_NULL && super->is_nullable) {
        return 1;
    }
    
    if (sub->kind == DEUS_TYPE_KIND_LIST && super->kind == DEUS_TYPE_KIND_LIST) {
        return deus_type_is_subtype_of(sub->element_type, super->element_type);
    }
    
    if ((sub->kind == DEUS_TYPE_KIND_FUTURE || sub->kind == DEUS_TYPE_KIND_OPTIONAL) &&
        (super->kind == DEUS_TYPE_KIND_FUTURE || super->kind == DEUS_TYPE_KIND_OPTIONAL)) {
        return deus_type_is_subtype_of(sub->inner_type, super->inner_type);
    }
    
    return 0;
}

const char *deus_type_to_string(const DeusType *type, char *buffer, size_t capacity) {
    if (!type || !buffer || capacity == 0) return buffer;
    buffer[0] = '\0';
    
    switch (type->kind) {
        case DEUS_TYPE_KIND_NULL: snprintf(buffer, capacity, "Null"); break;
        case DEUS_TYPE_KIND_BOOL: snprintf(buffer, capacity, "Bool"); break;
        case DEUS_TYPE_KIND_I64: snprintf(buffer, capacity, "I64"); break;
        case DEUS_TYPE_KIND_STRING: snprintf(buffer, capacity, "String"); break;
        case DEUS_TYPE_KIND_DOCUMENT: snprintf(buffer, capacity, "Document"); break;
        case DEUS_TYPE_KIND_FUTURE: {
            char inner[64];
            deus_type_to_string(type->inner_type, inner, sizeof(inner));
            snprintf(buffer, capacity, "Future<%s>", inner);
            break;
        }
        case DEUS_TYPE_KIND_ERROR: snprintf(buffer, capacity, "Error"); break;
        case DEUS_TYPE_KIND_LIST: {
            char elem[64];
            deus_type_to_string(type->element_type, elem, sizeof(elem));
            snprintf(buffer, capacity, "List<%s>", elem);
            break;
        }
        case DEUS_TYPE_KIND_RECORD: {
            snprintf(buffer, capacity, "Record{");
            size_t pos = strlen(buffer);
            for (uint32_t i = 0; i < type->field_count && pos < capacity - 1; i++) {
                if (i > 0) {
                    snprintf(buffer + pos, capacity - pos, ", ");
                    pos += 2;
                }
                char field_type[64];
                deus_type_to_string(type->fields[i].type, field_type, sizeof(field_type));
                int written = snprintf(buffer + pos, capacity - pos, "%.*s: %s",
                                       type->fields[i].name_length, type->fields[i].name, field_type);
                if (written > 0) pos += (size_t)written;
            }
            if (pos < capacity - 1) buffer[pos++] = '}';
            if (pos < capacity) buffer[pos] = '\0';
            break;
        }
        case DEUS_TYPE_KIND_OPTIONAL: {
            char inner[64];
            deus_type_to_string(type->inner_type, inner, sizeof(inner));
            snprintf(buffer, capacity, "%s?", inner);
            break;
        }
        case DEUS_TYPE_KIND_DYNAMIC: snprintf(buffer, capacity, "Dynamic"); break;
    }
    return buffer;
}
