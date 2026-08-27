# DEUS Language Specification

Version: 0.2 (Draft)
Status: Under active development

## 1. Overview

DEUS is a specialized language for bounded information acquisition, retrieval,
extraction, composition, filtering, evaluation, ranking, and explanation.

### 1.1 Design Principles

1. **Bounded execution**: All programs must have explicit resource limits
2. **Capability-based authority**: Naming modules does not grant permissions
3. **Immutable by default**: `bind` creates immutable locals
4. **Explicit effects**: Pure expressions cannot perform host effects
5. **Structured retrieval**: Domain-specific constructs for search workflows
6. **No general-purpose control flow**: No loops, conditionals, or recursion

### 1.2 Execution Model

```
source (.deus)
    → lexical analysis (tokens)
    → structural parsing (Source AST with layout events)
    → semantic validation (type checking, capability checks)
    → lowering (Core AST / instructions)
    → bytecode generation (.deusb, ABI v2)
    → bounded VM execution
    → authorized host effects
```

## 2. Lexical Structure

### 2.1 Tokens

- **Identifiers**: `[a-zA-Z_][a-zA-Z0-9_]*` (may end with `?` for safe accessors)
- **Strings**: `"..."` with escapes `\n`, `\t`, `\"`, `\\`
- **Integers**: `[0-9]+` or `-[0-9]+` (signed 64-bit range)
- **Booleans**: `true`, `false`
- **Null**: `null`
- **Operators**: `=`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `??`, `or`, `and`, `not`
- **Delimiters**: `{`, `}`, `[`, `]`, `(`, `)`, `:`, `,`
- **Comments**: `# ...` or `// ...` (line comments only)

### 2.2 Layout

- Indentation: exactly 4 spaces per level (tabs are invalid)
- Newlines: LF, CR, or CRLF
- Blank lines and comment-only lines preserve indentation depth
- Structured literals (`{}`, `[]`) suspend indentation tracking

## 3. Program Structure

### 3.1 Modern Syntax (Flow-based)

```ebnf
program          = modern_program | legacy_program ;

modern_program   = flow_header , newline , flow_body ;
flow_header      = "flow" , identifier , ":" ;
flow_body        = { blank_line | comment_line | flow_item } ;
flow_item        = indent_1 , ( limits_block | statement ) , newline ;

limits_block     = "limits:" , newline , { indent_2 , limits_entry , newline } ;
limits_entry     = limits_name , unsigned_integer ;
limits_name      = "workers" | "retry" | "backoff" | "rate" ;
```

Example:
```deus
flow search:
    limits:
        workers 8
        retry 3
        backoff 100
        rate 20

    bind page = hunt "https://example.test"
    bind title = reap page "h1"
    load title
    emit
    halt
```

### 3.2 Legacy Syntax (Flat)

```ebnf
legacy_program   = { statement } ;
statement        = instruction | bind | load | set | push ;
```

Example:
```deus
omni "net.http2"
genesis
limit 8
bind page = hunt "https://example.test"
load page
emit
halt
```

### 3.3 Flow Declaration Rules

1. Exactly one `flow` declaration per file (modern syntax)
2. `limits:` block (if present) must precede all network effects
3. At most one `limits:` block per flow
4. Legacy executor settings may not mix with `limits:`

## 4. Types

### 4.1 Core Types

| Type | Description | Serializable |
|------|-------------|--------------|
| `Null` | Absence of value | Yes |
| `Bool` | `true` or `false` | Yes |
| `I64` | Signed 64-bit integer | Yes |
| `String` | UTF-8 text | Yes |
| `List<T>` | Ordered collection | If T is serializable |
| `Record` | Named fields | If all fields are serializable |
| `Document` | Parsed HTML/XML | No |
| `Future<T>` | Async result | No |
| `Error` | Structured error | Yes (with schema) |

### 4.2 Optional Types

`T?` represents `Null | T`. Safe accessors return optional types:

- `get? record "field"` → `T?`
- `at? list index` → `T?`

### 4.3 Type Compatibility

```
Null ≤ T?           (Null is subtype of any optional)
T ≤ T?              (Any type is subtype of its optional)
T? ≤ U?             (If T ≤ U, then T? ≤ U?)
```

## 5. Statements

### 5.1 Instructions

| Instruction | Operand | Description |
|-------------|---------|-------------|
| `genesis` | — | Marks program entry (exactly one required) |
| `await` | — | Wait for all pending futures |
| `emit` | — | Output top of stack to stdout (must be serializable) |
| `debug` | — | Write top of stack to stderr (must be serializable) |
| `halt` | — | Terminate program (exactly one required) |
| `hunt` | string URL | Fetch URL, returns `Document` or `Future<Document>` |
| `reap` | local, string CSS | Extract from document, returns `String` or `List<String>` |
| `fork` | string URL | Start async fetch, returns `Future<Document>` |
| `join` | N | Wait for N most recent futures |
| `limit` | N | Set worker concurrency (1–256) |
| `retry` | N | Set retry count (0–16) |
| `backoff` | N | Set backoff in ms (0–60000) |
| `rate` | N | Set rate limit req/s (0–10000) |
| `omni` | string module | Declare module dependency |

### 5.2 Bind Statement

Creates an immutable local variable:

```deus
bind name = expression
```

Expression types:
- Literal values: `bind x = 42`
- Structured literals: `bind r = {"key": "value"}`
- Empty structures: `bind r = record`, `bind l = list`
- Hunt: `bind doc = hunt "url"`
- Reap: `bind text = reap doc "selector"`
- JSON: `bind obj = json doc "$.path"`

JSON extracted from a Document is dynamic. Hosts may preflight known scalar shapes with the additive [JSON contract API](JSON_CONTRACTS.md) before executing a flow.
- Get: `bind val = get record "field"` (errors if missing)
- Get?: `bind val = get? record "field"` (returns `Null` if missing)
- At: `bind item = at list index` (errors if out of bounds)
- At?: `bind item = at? list index` (returns `Null` if out of bounds)
- Pure expressions: `bind result = a + b * 2`

### 5.2.1 Host Adapter Calls

```deus
bind output = call "adapter.operation" input
```

`call` is the generic capability-gated bridge to the embedding host. It does
not grant network, model, database, or filesystem access by itself: the host
must expose `DEUS_HOST_CAP_ADAPTER_CALL` and implement the `DeusHost.call`
callback. Adapter names begin with a lowercase letter and may contain lowercase
letters, digits, `.` and `-`.

The input and output must be serializable DEUS values (`null`, booleans, i64,
UTF-8 strings, lists, and records). `Document`, `Future`, bytes, and errors are
not accepted across this boundary. The VM provides the callback a value context;
the callback must construct its output in that context. Outputs remain owned by
the VM after the callback returns.

### 5.2.2 Dynamic Values and Conversion

Adapter calls and untyped JSON extraction produce dynamic values. A dynamic value
must be explicitly converted with `bool(...)`, `i64(...)`, or `text(...)` before
it can be used in a Boolean or ordering expression. This keeps runtime adapter
contracts at the boundary instead of letting uncertain types flow through logic.
Dynamic structured reads can refine an adapter result: `get` and `at` work when
the runtime value is respectively a record or list. The optional forms, `get?`
and `at?`, return `null` for an absent key/index or an incompatible runtime kind;
their mandatory counterparts fail.

For example, EDEN can use `call "vision.describe" query` and then
`call "catalog.search" description`. Those names are application conventions,
not language keywords.

### 5.3 Load Statement

Pushes a local variable onto the stack:

```deus
load name
```

### 5.4 Set Statement

Mutates a record field:

```deus
set record "field" value
```

Requirements:
- `record` must be a `LOCAL_VALUE` (mutable reference)
- Used internally for structured literal desugaring

### 5.5 Push Statement

Appends to a list:

```deus
push list value
```

Requirements:
- `list` must be a `LOCAL_VALUE` (mutable reference)
- Used internally for structured literal desugaring

## 6. Expressions

### 6.1 Operator Precedence (lowest to highest)

1. Coalescing: `??` (right-associative)
2. Logical OR: `or` (left-associative)
3. Logical AND: `and` (left-associative)
4. Equality: `==`, `!=` (left-associative)
5. Comparison: `<`, `<=`, `>`, `>=` (left-associative)
6. Unary: `not`
7. Primary: literals, identifiers, conversions, parenthesized expressions

### 6.2 Arithmetic Operators

- Addition: `+`
- Subtraction: `-`
- Multiplication: `*`
- Division: `/` (integer division, truncates toward zero)
- Modulo: `%` (remainder)

All arithmetic operations check for overflow and fail explicitly.
Division by zero fails at runtime.

Precedence (highest to lowest):
1. Unary: `-` (negation), `not`
2. Multiplicative: `*`, `/`, `%` (left-associative)
3. Additive: `+`, `-` (left-associative)
4. Comparison: `<`, `<=`, `>`, `>=`
5. Equality: `==`, `!=`
6. Logical AND: `and`
7. Logical OR: `or`
8. Coalescing: `??` (right-associative)

### 6.3 Conversions

```deus
text(expression)    # Convert to String
i64(expression)     # Convert to I64
bool(expression)    # Convert to Bool
```

Conversion rules:
- `text(Null)` → `""`
- `text(Bool)` → `"true"` or `"false"`
- `text(I64)` → decimal representation
- `i64(String)` → parse decimal (fails if invalid)
- `i64(Bool)` → `0` or `1`
- `bool(Null)` → `false`
- `bool(I64)` → `false` if 0, else `true`
- `bool(String)` → `false` if empty, else `true`

### 6.4 Member Access

Syntax: `record.field`

Desugars to: `get record "field"`

Safe member access: `record?field`
Desugars to: `get? record "field"`

Member access has higher precedence than all binary operators.

Example:
```deus
bind person = {"name": "Alice", "age": 30}
bind name = person.name          # desugars to: get person "name"
bind maybe = person?email        # desugars to: get? person "email"
bind result = person.name ?? "unknown"  # coalesce on safe access
```

### 6.5 Item Access

Syntax: `list[index]`

Desugars to: `at list index`

Safe item access: `list?[index]`
Desugars to: `at? list index`

Item access has higher precedence than all binary operators.

Example:
```deus
bind numbers = [1, 2, 3]
bind first = numbers[0]          # desugars to: at numbers 0
bind maybe = numbers?[5]         # desugars to: at? numbers 5
bind sum = numbers[0] + numbers[1]  # arithmetic on items
```

## 7. Structured Literals

### 7.1 Record Literals

```deus
bind person = {
    "name": "Alice",
    "age": 30,
    "active": true
}
```

Desugars to:
```deus
bind __temp_1 = record
set __temp_1 "name" "Alice"
set __temp_1 "age" 30
set __temp_1 "active" true
bind person = __temp_1
```

### 7.2 List Literals

```deus
bind numbers = [1, 2, 3]
```

Desugars to:
```deus
bind __temp_2 = list
push __temp_2 1
push __temp_2 2
push __temp_2 3
bind numbers = __temp_2
```

### 7.3 Nested Structures

```deus
bind config = {
    "server": {
        "host": "localhost",
        "port": 8080
    },
    "features": ["auth", "logging"]
}
```

## 8. Semantic Rules

### 8.1 Immutable Locals

- `bind` creates an immutable binding
- Locals cannot be reassigned
- Mutable state exists only via `set` and `push` on `LOCAL_VALUE`

### 8.2 Single Entry and Exit

- Exactly one `genesis` per program
- Exactly one `halt` per program
- `genesis` must precede all other instructions
- `halt` must be the final instruction

### 8.3 Stack Discipline

- All values flow through the stack
- `bind` pops from stack and creates local
- `load` pushes local to stack
- `emit` and `debug` consume top of stack
- Compiler validates stack balance

### 8.4 Capability Checks

- `hunt`, `reap`, `fork` require `net.http2` module
- Host must authorize capabilities at runtime
- Module declaration (`omni`) does not grant authority

### 8.5 Resource Limits

Default limits (if not specified):
- `workers`: 4
- `retry`: 0
- `backoff`: 0
- `rate`: unlimited

Validation ranges:
- `workers`: 1–256
- `retry`: 0–16
- `backoff`: 0–60000 (milliseconds)
- `rate`: 0–10000 (requests/second)

### 8.6 Serialization Validation

`emit` and `debug` require serializable values:

**Serializable**: `Null`, `Bool`, `I64`, `String`, `List<serializable>`, `Record{serializable}`, `Error`

**Not serializable**: `Document`, `Future<T>`, raw function references

Compiler must reject:
```deus
bind doc = hunt "url"
load doc
emit  # ERROR: Document is not serializable
```

Must extract first:
```deus
bind doc = hunt "url"
bind title = reap doc "h1"
load title
emit  # OK: String is serializable
```

## 9. Error Handling

### 9.1 Compile-time Errors

- Syntax errors (invalid tokens, structure)
- Type errors (incompatible operations)
- Missing `genesis` or `halt`
- Multiple `genesis` or `halt`
- Unbalanced stack
- Invalid resource limits
- Duplicate `limits:` entries
- Unknown limit names
- Mixing legacy and modern executor settings
- Non-serializable `emit`

### 9.2 Runtime Errors

- Network failures (respecting `retry` and `backoff`)
- Rate limit exceeded
- Worker exhaustion
- Out of memory
- Stack overflow
- Invalid type operations (VM-level)

### 9.3 Error Values

Structured errors (future feature):
```deus
bind result = hunt "url"
# result type: Future<Document> | Error
```

## 10. Bytecode ABI

See [BYTECODE.md](BYTECODE.md) for physical encoding specification.

Key opcodes:
- `OP_GENESIS`, `OP_HALT`
- `OP_EMIT`, `OP_AWAIT`
- `OP_HUNT`, `OP_REAP`, `OP_FORK`, `OP_JOIN`
- `OP_LIMIT`, `OP_RETRY`, `OP_BACKOFF`, `OP_RATE`
- `OP_BIND_LOCAL_SCALAR`, `OP_BIND_LOCAL_VALUE`
- `OP_LOAD_LOCAL_SCALAR`, `OP_LOAD_LOCAL_VALUE`
- `OP_SET_FIELD`, `OP_PUSH_ITEM`
- `OP_NULL`, `OP_BOOL`, `OP_I64`, `OP_STRING`

## 11. Future Extensions (RFC Required)

The following features require dedicated RFCs before implementation:

1. **Arithmetic expressions** (`+`, `-`, `*`, `/`, `%`; proposed in [RFC-0002](RFC-0002-CHECKED-I64-ARITHMETIC.md))
2. **Member access syntax** (`record.field`)
3. **Item access syntax** (`list[index]`)
4. **Pipeline construct** (`pipeline items as item: filter ... rank ... take`)
5. **Parallel blocks** (`parallel:` for concurrent constant-URL hunts)
6. **Rules** (`rule name(subject): expression`)
7. **Provenance schema** (explicit authenticated metadata)
8. **Typed collections** (`List<T>`, `Record{field: Type}`)
9. **Collection iteration** (bounded, budgeted)
10. **Stable sorting** (for deterministic ranking)

## 12. Examples

### 12.1 Minimal Program

```deus
flow hello:
    bind greeting = "Hello, World!"
    load greeting
    emit
    halt
```

### 12.2 Web Scraping

```deus
flow scraper:
    limits:
        workers 4
        retry 2
        backoff 500

    omni "net.http2"
    
    genesis
    
    bind home = hunt "https://example.test"
    bind title = reap home "title"
    bind links = reap home "a[href]"
    
    load title
    emit
    halt
```

### 12.3 Data Processing

```deus
flow processor:
    bind data = {
        "users": [
            {"name": "Alice", "score": 95},
            {"name": "Bob", "score": 87},
            {"name": "Charlie", "score": 92}
        ]
    }
    
    bind users = get data "users"
    bind first = at users 0
    bind name = get first "name"
    
    load name
    emit
    halt
```

### 12.4 Error Handling with Optional

```deus
flow safe_extractor:
    omni "net.http2"
    genesis
    
    bind doc = hunt "https://example.test"
    bind main = get? doc "#main"
    bind fallback = get? doc "body"
    bind content = main ?? fallback
    
    load content
    emit
    halt
```

## Appendix A: Reserved Keywords

```
and, await, backoff, bind, bool, debug, emit, false, flow, fork, get, get?,
genesis, halt, hunt, i64, if, in, join, limit, limits, list, load,
not, null, omni, or, parallel, pipeline, push, rate, reap, record,
retry, rule, set, take, text, true, workers
```

## Appendix B: Standard Library Modules (Planned)

- `net.http2`: HTTP/2 client for web retrieval
- `html.parser`: HTML parsing and CSS selectors
- `json.path`: JSONPath queries
- `text.regex`: Regular expression matching
- `crypto.hash`: Hash functions (SHA-256, etc.)
- `time.clock`: Timestamps and durations

---

**Note**: This specification is a living document. Implementation status may vary.
Check individual source files and test cases for current behavior.
