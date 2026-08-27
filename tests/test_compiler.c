#include "deus_compiler.h"

#include <stdio.h>
#include <string.h>

static int require(int condition, const char *message) {
    if (!condition) fprintf(stderr, "compiler test: %s\n", message);
    return condition;
}

static int test_lexer(void) {
    const char *source = "# head\nomni \"net\\nhttp2\" // tail\nlimit 8";
    DeusLexer lexer; DeusToken token; DeusDiagnostic diagnostic = {0};
    deus_lexer_init(&lexer, source, strlen(source));
    if (!require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == DEUS_TOKEN_IDENTIFIER && token.line == 2u, "identifier token")) return 0;
    if (!require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == DEUS_TOKEN_STRING && token.length == 9u, "decoded string token")) return 0;
    deus_token_dispose(&token);
    if (!require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == DEUS_TOKEN_IDENTIFIER && token.line == 3u, "comment position")) return 0;
    if (!require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == DEUS_TOKEN_NUMBER && token.number == 8u, "number token")) return 0;
    return require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == DEUS_TOKEN_EOF, "EOF token");
}

static int test_expression_lexer(void) {
    const char *source = "(score >= 80 and verified) ?? false == true != false < 2 <= 3 > 1";
    const DeusTokenKind expected[] = {
        DEUS_TOKEN_LPAREN, DEUS_TOKEN_IDENTIFIER, DEUS_TOKEN_GREATER_EQUAL,
        DEUS_TOKEN_NUMBER, DEUS_TOKEN_IDENTIFIER, DEUS_TOKEN_IDENTIFIER,
        DEUS_TOKEN_RPAREN, DEUS_TOKEN_COALESCE, DEUS_TOKEN_IDENTIFIER,
        DEUS_TOKEN_EQUAL_EQUAL, DEUS_TOKEN_IDENTIFIER, DEUS_TOKEN_NOT_EQUAL,
        DEUS_TOKEN_IDENTIFIER, DEUS_TOKEN_LESS, DEUS_TOKEN_NUMBER,
        DEUS_TOKEN_LESS_EQUAL, DEUS_TOKEN_NUMBER, DEUS_TOKEN_GREATER, DEUS_TOKEN_NUMBER
    };
    DeusLexer lexer; DeusToken token = {0}; DeusDiagnostic diagnostic = {0};
    deus_lexer_init(&lexer, source, strlen(source));
    for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); index++) {
        if (!require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == expected[index],
                     "expression token")) return 0;
        deus_token_dispose(&token);
    }
    return require(deus_lexer_next(&lexer, &token, &diagnostic) && token.kind == DEUS_TOKEN_EOF,
                   "expression EOF");
}

static int test_expression_ast(void) {
    const char *source = "verified and score >= 80 ?? bool(0)";
    DeusExpressionNode *expression = NULL; DeusDiagnostic diagnostic = {0};
    if (!require(deus_parse_expression(source, strlen(source), &expression, &diagnostic),
                 "recursive expression parse")) return 0;
    int ok = expression->kind == DEUS_EXPRESSION_BINARY &&
             expression->operator_kind == DEUS_EXPRESSION_OP_COALESCE &&
             expression->left && expression->left->operator_kind == DEUS_EXPRESSION_OP_AND &&
             expression->left->right && expression->left->right->operator_kind == DEUS_EXPRESSION_OP_GREATER_EQUAL &&
             expression->right && expression->right->kind == DEUS_EXPRESSION_CONVERSION;
    deus_expression_free(expression); return require(ok, "expression precedence tree");
}

static int test_grammar_surface(void) {
    const char *source =
        "omni \"net.http2\"\n"
        "genesis\n"
        "limit 8\nretry 2\nbackoff 100\nrate 20\n"
        "bind _scalar = -42\n"
        "bind truth = not false or true and _scalar >= -42 == true ?? false\n"
        "bind converted = text(i64(\"42\"))\n"
        "bind object = {\"name\": \"DEUS\", \"nested\": [true, null, _scalar]}\n"
        "bind empty_record = record\n"
        "bind empty_list = list\n"
        "set empty_record \"answer\" _scalar\n"
        "push empty_list _scalar\n"
        "bind page = hunt \"https://example.test\"\n"
        "bind title = reap page \"h1\"\n"
        "bind value = json page \"$.value\"\n"
        "bind field = get object \"name\"\n"
        "bind optional_field = get? object \"missing\"\n"
        "bind first = at empty_list 0\n"
        "bind optional_item = at? empty_list 1\n"
        "load title\ndebug\n"
        "load title\nemit\n"
        "fork \"https://example.test/a\"\nawait\n"
        "fork \"https://example.test/b\"\njoin 1\n"
        "hunt \"https://example.test/c\"\nreap \"h1\"\nemit\n"
        "halt\n";
    const char *arbitrary_call = "genesis\nbind value = custom(1)\nhalt\n";
    DeusAstProgram ast; DeusDiagnostic diagnostic = {0};
    if (!require(deus_parse_ast(source, strlen(source), &ast, &diagnostic),
                 "complete documented grammar surface")) return 0;
    deus_ast_free(&ast);
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!require(!deus_parse_ast(arbitrary_call, strlen(arbitrary_call), &ast, &diagnostic),
                 "arbitrary function calls are outside the grammar")) return 0;
    return require(strstr(diagnostic.message, "unexpected token") != NULL,
                   "arbitrary call diagnostic");
}

static int test_ast(void) {
    const char *source = "omni \"net.http2\"\ngenesis\nhunt \"https://example.test\"\nreap \"h1\"\nemit\nhalt\n";
    DeusAstProgram ast; DeusDiagnostic diagnostic = {0};
    if (!require(deus_parse_ast(source, strlen(source), &ast, &diagnostic), "AST parse")) return 0;
    if (!require(ast.count == 6u && ast.instructions[2].opcode == DEUS_HUNT &&
                 ast.instructions[2].operand_kind == DEUS_AST_OPERAND_STRING &&
                 ast.instructions[2].line == 3u, "AST instruction shape")) { deus_ast_free(&ast); return 0; }
    deus_ast_free(&ast); return 1;
}

static int test_diagnostic(void) {
    const char *source = "genessis\nhalt\n";
    DeusAstProgram ast; DeusDiagnostic diagnostic = {0};
    if (!require(!deus_parse_ast(source, strlen(source), &ast, &diagnostic), "unknown instruction rejection")) return 0;
    return require(diagnostic.line == 1u && diagnostic.column == 1u && strstr(diagnostic.message, "did you mean 'genesis'") != NULL,
                   "diagnostic suggestion");
}

static int test_locals(void) {
    const char *source = "genesis\nbind query = \"frieren\"\nload query\nemit\nhalt\n";
    const char *invalid = "genesis\nload missing\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0};
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "local compilation")) return 0;
    if (!require(program.code_count == 6u && program.code[1].opcode == DEUS_CONST &&
                 program.code[2].opcode == DEUS_BIND && program.code[2].operand == 0u &&
                 program.code[3].opcode == DEUS_LOAD && program.strings[0].len == 7u,
                 "local bytecode")) { deus_program_free(&program); return 0; }
    deus_program_free(&program);
    if (!require(!deus_parse_source(invalid, strlen(invalid), &program, &diagnostic) &&
                 strstr(diagnostic.message, "unknown local") != NULL, "unknown local rejection")) return 0;
    source = "genesis\nbind year = -42\nbind safe = true\nbind missing = null\nbind copy = year\nhalt\n";
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "scalar compilation")) return 0;
    int ok = program.code_count == 10u && program.code[1].opcode == DEUS_CONST_I64 &&
             program.code[1].immediate == -42 && program.code[3].opcode == DEUS_CONST_BOOL &&
             program.code[5].opcode == DEUS_CONST_NULL && program.code[7].opcode == DEUS_LOAD;
    deus_program_free(&program); return require(ok, "scalar bytecode");
}

static int test_operational_expressions(void) {
    const char *source = "omni \"net.http2\"\ngenesis\nbind page = hunt \"deus://catalog/test\"\nbind title = reap page \"h1\"\nload title\nemit\nhalt\n";
    const char *invalid = "genesis\nbind value = 42\nbind title = reap value \"h1\"\nhalt\n";
    const char *late_config = "genesis\nhunt \"https://example.test\"\nlimit 2\nreap \"h1\"\nemit\nhalt\n";
    const char *document_emit = "genesis\nbind page = hunt \"https://example.test\"\nload page\nemit\nhalt\n";
    const char *document_debug = "genesis\nbind page = hunt \"https://example.test\"\nload page\ndebug\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0};
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "operational expression compilation")) return 0;
    int ok = program.code_count == 10u && program.code[2].opcode == DEUS_HUNT &&
             program.code[3].opcode == DEUS_BIND && program.code[4].opcode == DEUS_LOAD &&
             program.code[5].opcode == DEUS_REAP && program.code[6].opcode == DEUS_BIND;
    deus_program_free(&program);
    if (!require(ok, "operational expression lowering")) return 0;
    if (!require(!deus_parse_source(invalid, strlen(invalid), &program, &diagnostic) &&
                 strstr(diagnostic.message, "Document") != NULL, "reap type rejection")) return 0;
    if (!require(!deus_parse_source(late_config, strlen(late_config), &program, &diagnostic) &&
                 strstr(diagnostic.message, "network execution") != NULL, "late executor configuration rejection")) return 0;
    if (!require(!deus_parse_source(document_emit, strlen(document_emit), &program, &diagnostic) &&
                 strstr(diagnostic.message, "serializable") != NULL, "Document emit type rejection")) return 0;
    return require(!deus_parse_source(document_debug, strlen(document_debug), &program, &diagnostic) &&
                   strstr(diagnostic.message, "serializable") != NULL, "Document debug type rejection");
}
static int test_typed_expressions(void) {
    const char *source = "genesis\nbind score = 95\nbind minimum = 80\nbind verified = true\nbind eligible = verified and score >= minimum\nbind total = score + minimum * 2\nbind label = null ?? \"fallback\"\nbind score_text = text(score)\nbind parsed = i64(\"42\")\nbind truth = bool(1)\nhalt\n";
    const char *invalid = "genesis\nbind bad = 1 and true\nhalt\n";
    const char *dynamic_invalid = "genesis\nbind input = \"42\"\nbind result = call \"demo.value\" input\nbind bad = result + 1\nhalt\n";
    const char *dynamic_converted = "genesis\nbind input = \"42\"\nbind result = call \"demo.value\" input\nbind score = i64(result)\nbind accepted = score + 1\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; int comparisons = 0, boolean = 0, fallback = 0, conversions = 0, arithmetic = 0;
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "typed expression compilation")) return 0;
    for (uint32_t index = 0; index < program.code_count; index++) {
        uint8_t opcode = program.code[index].opcode;
        if (opcode >= DEUS_EQUAL && opcode <= DEUS_GREATER_EQUAL) comparisons++;
        if (opcode == DEUS_BOOL_NOT || opcode == DEUS_BOOL_AND || opcode == DEUS_BOOL_OR) boolean++;
        if (opcode == DEUS_COALESCE) fallback++;
        if (opcode >= DEUS_ADD_I64 && opcode <= DEUS_MOD_I64) arithmetic++;
        if (opcode >= DEUS_TO_TEXT && opcode <= DEUS_TO_BOOL) conversions++;
    }
    deus_program_free(&program);
    if (!require(comparisons == 1 && boolean == 1 && fallback == 1 && conversions == 3 && arithmetic == 2,
                 "typed expression lowering")) return 0;
    if (!require(!deus_parse_source(invalid, strlen(invalid), &program, &diagnostic) &&
                 strstr(diagnostic.message, "Bool") != NULL, "boolean type rejection")) return 0;
    if (!require(!deus_parse_source(dynamic_invalid, strlen(dynamic_invalid), &program, &diagnostic) &&
                 strstr(diagnostic.message, "arithmetic requires I64") != NULL, "dynamic arithmetic requires conversion")) return 0;
    if (!require(deus_parse_source(dynamic_converted, strlen(dynamic_converted), &program, &diagnostic),
                 "converted dynamic arithmetic compilation")) return 0;
    deus_program_free(&program); return 1;
}
static int test_dynamic_scalar_narrowing(void) {
    const char *ordering = "genesis\nbind page = hunt \"https://example.test\"\nbind score = json page \"$.score\"\nbind eligible = score >= 80\nhalt\n";
    const char *boolean = "genesis\nbind page = hunt \"https://example.test\"\nbind verified = json page \"$.verified\"\nbind eligible = not verified\nhalt\n";
    const char *narrowed = "genesis\nbind page = hunt \"https://example.test\"\nbind score = json page \"$.score\"\nbind verified = json page \"$.verified\"\nbind eligible = bool(verified) and i64(score) >= 80\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0};
    if (!require(!deus_parse_source(ordering, strlen(ordering), &program, &diagnostic) &&
                 strstr(diagnostic.message, "I64") != NULL && diagnostic.line == 4u,
                 "dynamic scalar ordering requires i64 narrowing")) return 0;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!require(!deus_parse_source(boolean, strlen(boolean), &program, &diagnostic) &&
                 strstr(diagnostic.message, "Bool") != NULL && diagnostic.line == 4u,
                 "dynamic scalar boolean requires bool narrowing")) return 0;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!require(deus_parse_source(narrowed, strlen(narrowed), &program, &diagnostic),
                 "explicitly narrowed dynamic scalars compile")) return 0;
    deus_program_free(&program);
    return 1;
}
static int test_url_templates(void) {
    const char *source = "genesis\nbind query = \"frieren white\"\nbind year = 2023\nbind page = hunt \"https://example.test/search?q={query}&year={year}\"\nhalt\n";
    const char *unknown = "genesis\nbind page = hunt \"https://example.test/{missing}\"\nhalt\n";
    const char *document = "genesis\nbind page = hunt \"https://example.test\"\nbind other = hunt \"https://example.test/{page}\"\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; int encoded = 0, dynamic_hunt = 0;
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "URL template compilation")) return 0;
    for (uint32_t index = 0; index < program.code_count; index++) {
        if (program.code[index].opcode == DEUS_URL_ENCODE) encoded++;
        if (program.code[index].opcode == DEUS_HUNT_VALUE) dynamic_hunt++;
    }
    deus_program_free(&program);
    if (!require(encoded == 2 && dynamic_hunt == 1, "URL template lowering")) return 0;
    if (!require(!deus_parse_source(unknown, strlen(unknown), &program, &diagnostic) &&
                 strstr(diagnostic.message, "unknown local") != NULL, "unknown URL placeholder")) return 0;
    return require(!deus_parse_source(document, strlen(document), &program, &diagnostic) &&
                   strstr(diagnostic.message, "String, I64, or Bool") != NULL, "document URL placeholder rejection");
}


static int test_emit_serialization(void) {
    const char *document =
        "genesis\n"
        "bind page = hunt \"https://example.test\"\n"
        "load page\n"
        "emit\n"
        "halt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0};
    return require(!deus_parse_source(document, strlen(document), &program, &diagnostic) &&
                   strstr(diagnostic.message, "serializable value") != NULL && diagnostic.line == 4u,
                   "Document emit rejection");
}
static int test_structured_reads(void) {
    const char *source = "genesis\nbind item = {\"title\": \"Frieren\", \"meta\": {\"score\": 95}}\nbind items = [item, null]\nbind first = at items 0\nbind direct_title = item.title\nbind direct_score = item.meta.score\nbind direct_first = items[0]\nbind copied = get first \"title\"\nbind missing = get? first \"subtitle\"\nbind absent = at? items 99\nhalt\n";
    const char *wrong = "genesis\nbind title = \"Frieren\"\nbind copied = get title \"name\"\nhalt\n";
    const char *member_wrong = "genesis\nbind title = \"Frieren\"\nbind copied = title.name\nhalt\n";
    const char *index_wrong = "genesis\nbind items = [1]\nbind copied = items[-1]\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; int get = 0, at = 0, optional = 0, records = 0, scalar_constants = 0;
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "structured read compilation")) return 0;
    for (uint32_t index = 0; index < program.code_count; index++) {
        if (program.code[index].opcode == DEUS_RECORD_GET) get++;
        if (program.code[index].opcode == DEUS_LIST_AT) at++;
        if (program.code[index].opcode == DEUS_RECORD_GET_OPTIONAL ||
            program.code[index].opcode == DEUS_LIST_AT_OPTIONAL) optional++;
        if (program.code[index].opcode == DEUS_CONST_RECORD) records++;
        if (program.code[index].opcode == DEUS_CONST || program.code[index].opcode == DEUS_CONST_I64 ||
            program.code[index].opcode == DEUS_CONST_NULL) scalar_constants++;
    }
    deus_program_free(&program);
    if (!require(get == 4 && at == 2 && optional == 2 && records == 2 && scalar_constants == 3,
                 "nested structured literal and read lowering")) return 0;
    if (!require(!deus_parse_source(wrong, strlen(wrong), &program, &diagnostic) &&
                 strstr(diagnostic.message, "Record") != NULL, "legacy structured source type rejection")) return 0;
    if (!require(!deus_parse_source(member_wrong, strlen(member_wrong), &program, &diagnostic) &&
                 strstr(diagnostic.message, "member access requires Record") != NULL, "member access type rejection")) return 0;
    return require(!deus_parse_source(index_wrong, strlen(index_wrong), &program, &diagnostic) &&
                   strstr(diagnostic.message, "unsigned integer literal") != NULL, "item access index rejection");
}

static int test_indented_flow(void) {
    const char *source = "# modern source\nflow research:\n    bind score = 95\n    bind eligible = score >= 80\n    load eligible\n    emit\n";
    const char *bad_indent = "flow main:\n  bind value = 1\n";
    const char *tab_indent = "flow main:\n\tbind value = 1\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0};
    if (!require(deus_parse_source(source, strlen(source), &program, &diagnostic), "indented flow compilation")) return 0;
    int ok = program.code_count >= 2u && program.code[0].opcode == DEUS_GENESIS &&
             program.code[program.code_count - 1u].opcode == DEUS_HALT;
    deus_program_free(&program);
    if (!require(ok, "flow genesis and halt lowering")) return 0;
    if (!require(!deus_parse_source(bad_indent, strlen(bad_indent), &program, &diagnostic) &&
                 strstr(diagnostic.message, "four spaces") != NULL && diagnostic.line == 2u,
                 "short flow indentation rejection")) return 0;
    return require(!deus_parse_source(tab_indent, strlen(tab_indent), &program, &diagnostic) &&
                   strstr(diagnostic.message, "tabs") != NULL && diagnostic.line == 2u,
                   "tab flow indentation rejection");
}

int main(void) { return test_lexer() && test_expression_lexer() && test_expression_ast() && test_grammar_surface() && test_ast() && test_diagnostic() && test_locals() &&
                       test_operational_expressions() && test_typed_expressions() && test_dynamic_scalar_narrowing() && test_url_templates() &&
                       test_emit_serialization() && test_structured_reads() && test_indented_flow() ? 0 : 1; }
