#include "deus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
} } while (0)

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb"); long size; unsigned char *bytes;
    CHECK(file); CHECK(!fseek(file, 0, SEEK_END)); size = ftell(file); CHECK(size >= 0); rewind(file);
    bytes = (unsigned char *)malloc((size_t)size); CHECK(bytes || !size);
    CHECK(fread(bytes, 1u, (size_t)size, file) == (size_t)size); CHECK(!fclose(file));
    *length = (size_t)size; return bytes;
}

static void write_file(const char *path, const unsigned char *bytes, size_t length) {
    FILE *file = fopen(path, "wb"); CHECK(file);
    CHECK(fwrite(bytes, 1u, length, file) == length); CHECK(!fclose(file));
}

static void source_corpus(void) {
    static const char *cases[] = {
        "", "flow:\n", "flow main:\n\tbind x = 1\n", "flow main:\n  bind x = 1\n",
        "}}}}}}}\n", "bind bind bind\n", "999999999999999999999999999999999999\n",
        "flow main:\n    bind x = \"unterminated\n", "flow main:\n    parallel:\n",
        "genesis\njoin 4294967295\nhalt\n", "genesis\nload 999999\nhalt\n",
        "flow main:\n    bind value = [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[null]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]\n"
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        DeusProgram program; DeusDiagnostic diagnostic = {0};
        if (deus_parse_source(cases[index], strlen(cases[index]), &program, &diagnostic))
            deus_program_free(&program);
        else CHECK(diagnostic.message[0] != '\0');
    }
}

static void bytecode_corpus(void) {
    const char *valid_source =
        "flow main:\n"
        "    bind result = {\"title\": \"Frieren\", \"year\": 2023}\n"
        "    bind title = get result \"title\"\n"
        "    load title\n"
        "    emit\n";
    const char *valid_path = "deus_hostile_valid.deusb";
    const char *mutated_path = "deus_hostile_mutated.deusb";
    DeusProgram program, decoded; DeusDiagnostic diagnostic = {0}; char error[192];
    unsigned char *bytes; size_t length;
    CHECK(deus_parse_source(valid_source, strlen(valid_source), &program, &diagnostic));
    CHECK(deus_write_binary(&program, valid_path, error, sizeof(error))); deus_program_free(&program);
    CHECK(deus_read_binary(valid_path, &decoded, error, sizeof(error))); deus_program_free(&decoded);
    bytes = read_file(valid_path, &length); CHECK(length > DEUS_HEADER_SIZE);

    for (size_t cut = 0u; cut < length; cut++) {
        write_file(mutated_path, bytes, cut);
        CHECK(!deus_read_binary(mutated_path, &decoded, error, sizeof(error)));
    }
    for (size_t index = 0u; index < length; index++) {
        bytes[index] ^= 1u; write_file(mutated_path, bytes, length);
        CHECK(!deus_read_binary(mutated_path, &decoded, error, sizeof(error)));
        bytes[index] ^= 1u;
    }
    free(bytes); CHECK(!remove(valid_path)); CHECK(!remove(mutated_path));
}

int main(void) {
    source_corpus(); bytecode_corpus();
    puts("hostile source and bytecode corpus passed"); return 0;
}
