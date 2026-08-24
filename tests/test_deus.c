#include "deus.h"
#include <stdio.h>
#include <string.h>

static int parse_ok(const char *source, DeusProgram *program) {
    DeusDiagnostic d = {0};
    if (!deus_parse_source(source, strlen(source), program, &d)) {
        fprintf(stderr, "parse %u:%u: %s\n", d.line, d.column, d.message); return 0;
    }
    return 1;
}

int main(void) {
    const char *source =
        "omni \"net.http2\"\ngenesis\nlimit 4\nretry 2\nbackoff 10\nrate 100\n"
        "fork \"https://example.com/a\"\nfork \"https://example.com/b\"\n"
        "join 2\nreap \"h1\"\nemit\nreap \"h1\"\nemit\nhalt\n";
    DeusProgram p, q; char error[192];
    if (!parse_ok(source, &p)) return 1;
    if (!deus_write_binary(&p, "deus_test.deusb", error, sizeof(error)) ||
        !deus_read_binary("deus_test.deusb", &q, error, sizeof(error))) {
        fprintf(stderr, "format: %s\n", error); deus_program_free(&p); return 1;
    }
    int ok = p.string_count == q.string_count && p.code_count == q.code_count &&
             q.code[2].opcode == DEUS_LIMIT && q.code[2].operand == 4 &&
             q.code[8].opcode == DEUS_JOIN && q.code[8].operand == 2;
    deus_program_free(&p); deus_program_free(&q); remove("deus_test.deusb");

    const char *invalid = "genesis\nlimit 2\nfork \"http://x\"\nlimit 4\nawait\nhalt\n";
    DeusDiagnostic d = {0};
    if (deus_parse_source(invalid, strlen(invalid), &p, &d)) {
        deus_program_free(&p); fprintf(stderr, "late limit unexpectedly accepted\n"); return 1;
    }
    const char *locals = "genesis\nbind query = \"frieren\"\nbind year = -42\nload query\nemit\nload year\nemit\nhalt\n";
    if (!parse_ok(locals, &p) || p.code_count != 10u || p.code[1].opcode != DEUS_CONST ||
        p.code[2].opcode != DEUS_BIND || p.code[3].opcode != DEUS_CONST_I64 || p.code[3].immediate != -42) {
        deus_program_free(&p); fprintf(stderr, "local lowering failed\n"); return 1;
    }
    if (!deus_write_binary(&p, "deus_scalar_test.deusb", error, sizeof(error)) ||
        !deus_read_binary("deus_scalar_test.deusb", &q, error, sizeof(error)) ||
        q.code[3].opcode != DEUS_CONST_I64 || q.code[3].immediate != -42) {
        fprintf(stderr, "scalar format: %s\n", error); deus_program_free(&p); deus_program_free(&q); return 1;
    }
    deus_program_free(&p); deus_program_free(&q); remove("deus_scalar_test.deusb");
    const char *structured = "genesis\nbind title = \"Frieren\"\nbind item = record\nset item \"title\" title\nbind items = list\npush items item\nbind first = at items 0\nbind copied = get first \"title\"\nhalt\n";
    if (!parse_ok(structured, &p) ||
        !deus_write_binary(&p, "deus_structured_test.deusb", error, sizeof(error)) ||
        !deus_read_binary("deus_structured_test.deusb", &q, error, sizeof(error))) {
        fprintf(stderr, "structured format: %s\n", error); deus_program_free(&p); return 1;
    }
    int saw_get = 0, saw_at = 0;
    for (uint32_t index = 0; index < q.code_count; index++) {
        if (q.code[index].opcode == DEUS_RECORD_GET) saw_get++;
        if (q.code[index].opcode == DEUS_LIST_AT && q.code[index].operand == 0u) saw_at++;
    }
    deus_program_free(&p); deus_program_free(&q); remove("deus_structured_test.deusb");
    if (saw_get != 1 || saw_at != 1) { fprintf(stderr, "structured opcode round-trip failed\n"); return 1; }
    return ok ? 0 : 1;
}
