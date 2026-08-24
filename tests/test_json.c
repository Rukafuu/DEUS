#include "deus_json.h"
#include "deus.h"

#include <stdio.h>
#include <string.h>

static int require(int condition, const char *message) {
    if (!condition) fprintf(stderr, "json test: %s\n", message);
    return condition;
}

int main(void) {
    const char *json = "{\"results\":[{\"title\":\"Frieren \\uD83E\\uDD0D\",\"year\":2023,\"ok\":true,\"missing\":null}]}";
    DeusJsonScalar scalar; char error[192];
    if (!require(deus_json_extract_scalar(json, strlen(json), "$.results[0].title", 18u, &scalar, error, sizeof(error)), "nested string")) return 1;
    if (!require(scalar.kind == DEUS_JSON_STRING && scalar.string_length == 12u, "Unicode decoding")) return 1;
    deus_json_scalar_dispose(&scalar);
    if (!require(deus_json_extract_scalar(json, strlen(json), "$.results[0].year", 17u, &scalar, error, sizeof(error)) &&
                 scalar.kind == DEUS_JSON_I64 && scalar.integer == 2023, "integer extraction")) return 1;
    if (!require(deus_json_extract_scalar(json, strlen(json), "$.results[0].ok", 15u, &scalar, error, sizeof(error)) &&
                 scalar.kind == DEUS_JSON_BOOL && scalar.boolean, "boolean extraction")) return 1;
    if (!require(!deus_json_extract_scalar("{\"x\":1.5}", 9u, "$.x", 3u, &scalar, error, sizeof(error)) &&
                 strstr(error, "not an i64"), "fraction rejection")) return 1;
    if (!require(!deus_json_extract_scalar("{\"x\":[]}", 8u, "$.x", 3u, &scalar, error, sizeof(error)) &&
                 strstr(error, "compound"), "compound rejection")) return 1;
    char deep[132];
    for (size_t index = 0; index < 65u; index++) deep[index] = '[';
    deep[65] = '0';
    for (size_t index = 0; index < 65u; index++) deep[66u + index] = ']';
    deep[131] = '\0';
    if (!require(!deus_json_extract_scalar(deep, 131u, "$", 1u, &scalar, error, sizeof(error)) &&
                 strstr(error, "depth limit"), "depth rejection")) return 1;
    const unsigned char invalid_utf8[] = {'{', '"', 'x', '"', ':', '"', 0xC0u, 0xAFu, '"', '}'};
    if (!require(!deus_json_extract_scalar((const char *)invalid_utf8, sizeof(invalid_utf8), "$.x", 3u, &scalar, error, sizeof(error)) &&
                 strstr(error, "UTF-8"), "invalid UTF-8 rejection")) return 1;
    DeusString strings[] = {{"$.x", 3u}};
    DeusInstruction code[] = {{DEUS_GENESIS, 0u, 0}, {DEUS_JSON_PATH, 0u, 0}, {DEUS_HALT, 0u, 0}};
    DeusProgram program = {strings, 1u, code, 3u}, decoded = {0};
    if (!require(deus_write_binary(&program, "deus_json_test.deusb", error, sizeof(error)) &&
                 deus_read_binary("deus_json_test.deusb", &decoded, error, sizeof(error)), "JSON opcode round-trip")) return 1;
    int opcode_ok = decoded.code_count == 3u && decoded.code[1].opcode == DEUS_JSON_PATH && decoded.code[1].operand == 0u;
    deus_program_free(&decoded); remove("deus_json_test.deusb");
    if (!require(opcode_ok, "JSON opcode preservation")) return 1;
    return 0;
}
