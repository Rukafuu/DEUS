#ifndef DEUS_JSON_H
#define DEUS_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DEUS_JSON_NULL,
    DEUS_JSON_BOOL,
    DEUS_JSON_I64,
    DEUS_JSON_STRING
} DeusJsonScalarKind;

typedef struct {
    DeusJsonScalarKind kind;
    int boolean;
    int64_t integer;
    char *string;
    size_t string_length;
} DeusJsonScalar;

typedef struct {
    const char *path;
    size_t path_length;
    DeusJsonScalarKind kind;
    int nullable;
} DeusJsonScalarContract;

int deus_json_extract_scalar(const char *json, size_t json_length,
                             const char *path, size_t path_length,
                             DeusJsonScalar *out, char *error, size_t error_cap);
int deus_json_validate_scalar_contract(const char *json, size_t json_length,
                                       const DeusJsonScalarContract *fields,
                                       size_t field_count, char *error, size_t error_cap);
void deus_json_scalar_dispose(DeusJsonScalar *scalar);

#endif
