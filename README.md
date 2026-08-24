# DEUS — Decoupled Extended Unitary Script

<p align="center">
  <img src="assets/deus-logo.png" alt="DEUS language logo" width="260">
</p>

[![Windows](https://img.shields.io/badge/Platform-Windows-0078D4?logo=windows)](https://learn.microsoft.com/windows/)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake)](https://cmake.org/)
[![C17](https://img.shields.io/badge/Implementation-C17-00599C?logo=c)](https://en.wikipedia.org/wiki/C17_(C_standard_revision))
[![ABI](https://img.shields.io/badge/Bytecode_ABI-v1-8A2BE2)](docs/BYTECODE.md)
[![Status](https://img.shields.io/badge/Status-Experimental-FF6B6B)](#project-status)

DEUS is an experimental language for describing safe information acquisition,
retrieval, extraction, and composition pipelines. It is not a general-purpose
language and it is not a search engine by itself.

A DEUS program declares intent. The compiler validates that intent, the VM
enforces types and resource limits, and an authorized host performs environmental
work such as HTTP requests, index access, storage, embeddings, or media decoding.

```text
DEUS source → compiler → validated bytecode → bounded VM → authorized host
```

The reference implementation is native C17 for Windows, with no Node.js, Python,
Rust, or managed runtime dependency.

## A DEUS program in 30 seconds

New users should begin with [Getting Started](docs/GETTING_STARTED.md). The
documentation then continues through the [Language Tour](docs/LANGUAGE_TOUR.md),
[Cookbook](docs/COOKBOOK.md), and [Reference](docs/REFERENCE.md).

```deus
flow main:
    bind result = {
        "title": "Frieren",
        "year": 2023,
        "meta": {"verified": true}
    }

    bind title = get result "title"
    bind year = get result "year"
    bind subtitle = get? result "subtitle"
    bind eligible = year >= 2020
    bind display = subtitle ?? "Untitled"

    load result
    emit
```

DEUS supports typed locals, structured values, optional reads, pure expressions,
and compact JSON output. `emit` writes values directly without adding separators
or newlines, leaving output framing to the program or host.

`flow <name>:` is the modern source form. Its four-space indentation is
structural; `genesis` and `halt` are generated automatically. Flat legacy source
remains accepted during the migration period.

## Build and run

Requirements:

- Windows 10 or later;
- Visual Studio 2022 Build Tools with Desktop development with C++;
- CMake 3.20 or later.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run source directly:

```powershell
.\build\Release\deus.exe run examples\typed_expressions.deus
```

The unified CLI provides:

```text
deus run <file.deus>
deus check <file.deus>
deus build <file.deus> [-o file.deusb]
deus exec <file.deusb>
deus fmt [--check] <file.deus>
deus init <directory> [--template minimal|crawler|ranking]
deus version
deus help
```

`build` derives the `.deusb` path when `-o` is omitted. Diagnostics show the
source line and a caret at the failing column. `deusc.exe` and `deusvm.exe`
remain available as compatibility tools.

Install the executables into a standalone prefix:

```powershell
cmake --install build --config Release --prefix dist
```

The official logo is embedded in the Windows executables and installed under
`share/deus` in PNG and multi-resolution ICO formats. Source assets live in
`assets/`.

### VS Code extension

The language support under `editors/vscode` is a packageable extension with the
official logo, `.deus` file association, syntax highlighting, comments, brackets,
and auto-closing pairs:

```powershell
cd editors\vscode
npx @vscode/vsce package
code --install-extension deus-language-0.1.0.vsix
```

## Language concepts

### Typed values and locals

```deus
bind query = "frieren white hair"
bind year = 2023
bind verified = true
bind missing = null
bind copied_year = year
```

Names have program scope, cannot be rebound, and may only be introduced after
`genesis`. The value system provides `Null`, `Bool`, signed `I64`, UTF-8 `String`,
`Bytes`, `List`, `Record`, `Document`, `Future`, and structured `Error` values.

### Indented flows

```deus
flow research:
    bind score = 95
    bind eligible = score >= 80

    load eligible
    emit
```

Modern programs have a named top-level flow and a four-space body. Tabs are
rejected, indentation uses multiples of four, and blank/comment lines do not
change ownership. Structured literals may indent further. Phase 1 supports one
top-level flow; nested `limits`, `parallel`, `rule`, and pipeline blocks will build
on the same layout contract. See
[RFC-0001](docs/RFC-0001-INDENTED-FLOWS.md).

### Records and lists

```deus
bind candidate = {
  "title": "Frieren",
  "score": 95,
  "tags": ["elf", "mage"],
  "meta": {"verified": true}
}

bind candidates = [candidate]
bind first = at candidates 0
bind title = get first "title"
bind subtitle = get? first "subtitle"
bind tenth = at? candidates 9
```

`get` and `at` are strict; missing data is an error. `get?` and `at?` return
`Null` for absence, while a container of the wrong type remains an error.
Literals may contain scalars, previously bound locals, or nested literals and
are bounded to 32 structured levels. Explicit `record`, `list`, `set`, and
`push` forms remain available.

### Typed expressions

```deus
bind eligible = verified and score >= 80
bind changed = score != previous_score
bind display = subtitle ?? "Untitled"
bind score_text = text(score)
bind parsed_score = i64("95")
bind enabled = bool(1)
bind inverted = not false
```

Precedence, from tightest to loosest:

```text
parentheses and conversions
not
<  <=  >  >=
==  !=
and
or
??
```

Ordering currently requires `I64`. Equality accepts scalar values. Controlled
conversions accept `String`, `I64`, or `Bool`; integer text must be complete and
within range, while boolean text must be exactly `true` or `false`. Expressions
are limited to 64 nested levels and cannot perform host effects.

## Retrieval and extraction

### Host-backed crawling

```deus
omni "net.http2"
genesis
limit 8
retry 3
backoff 100
rate 20

bind query = "frieren white hair"
bind page = hunt "https://catalog.test/search?q={query}"
bind title = reap page "h1"

load title
emit
halt
```

`hunt` produces a `Document`; `reap` extracts text from it. URL placeholders
accept earlier `String`, `I64`, or `Bool` locals and are percent-encoded as URL
components. URLs are limited to 8 KiB.

The reference host uses WinHTTP with connection reuse, timeouts, gzip/deflate,
a 32 MiB response cap, and rejection of non-2xx responses.

### Concurrency and budgets

```deus
genesis
limit 4
retry 2
backoff 100
rate 10
fork "https://example.test/a"
fork "https://example.test/b"
join 2
reap "h1"
emit
reap "h1"
emit
halt
```

`FORK` uses event-driven futures on the Windows thread pool. `JOIN N` preserves
document order. Executor settings must precede network execution.

### Bounded JSON extraction

```deus
bind page = hunt "https://catalog.test/api?q={query}"
bind title = json page "$.results[0].title"
bind year = json page "$.results[0].year"
bind verified = json page "$.results[0].verified"
```

Paths support object fields and zero-based array indices. This version extracts
`String`, signed `I64`, `Bool`, or `Null`; compound JSON results, fractional
numbers, and exponent notation are not yet supported. Processing is bounded to
64 levels and 8,192 tokens, with UTF-8 and Unicode escape validation.

## Capability boundary

DEUS programs never gain authority merely by naming a module. Hosts declare
capabilities before execution. The public `DeusHost` ABI currently supports a
bounded network `hunt` capability through `deus_vm_execute_program_with_host`.

Successful host documents are borrowed. The VM copies them under its limits and
calls `release_document` exactly once. `FORK` callbacks may run concurrently, so
host implementations must be thread-safe.

Pure evaluation belongs to the VM. Credentials, HTTP policy, indexes, storage,
models, embeddings, and media decoding belong to authorized hosts. See
[DESIGN.md](DESIGN.md) for the project constitution and feature-admission rule.

## Safety and determinism

- fixed bytecode header, version checks, section validation, and CRC32;
- bounded memory, values, blobs, nesting, locals, URLs, and responses;
- reference-counted ownership with cycle and cross-context rejection;
- typed semantic analysis before bytecode generation;
- explicit host capability checks;
- no arbitrary filesystem access, processes, FFI, pointers, reflection, or raw
  threads in the language;
- deterministic joined-document order and record serialization.

## Architecture

```text
source.deus
    ├─ lexer
    ├─ statement parser + recursive Expression AST
    ├─ semantic analysis and type checks
    └─ ABI v1 bytecode generation
             ├─ deus run: execute in memory
             └─ deus build: write validated .deusb
                            ↓
                      bounded stack VM
                            ↓
                    authorized host capabilities
```

| Path | Responsibility |
| --- | --- |
| `include/deus.h` | Public bytecode and host ABI |
| `include/deus_value.h` | Bounded shared value API |
| `src/deus_lexer.c` | Tokens, positions, strings, and operators |
| `src/deus_expression.c` | Recursive expression parser and precedence |
| `src/deus_layout.c` | Significant-indentation validation and flow lowering |
| `src/deus_parser.c` | Statements, operational expressions, and literals |
| `src/deus_semantic.c` | Types, effects, lowering, and bytecode generation |
| `src/deus_format.c` | `.deusb` encoding, validation, and CRC32 |
| `src/deus_value.c` | Ownership, composites, limits, and JSON serialization |
| `src/deus_json.c` | Bounded JSON scalar extraction |
| `src/deusvm.c` | VM, concurrency, WinHTTP reference host, and extraction |
| `src/deus_cli.c` | Unified native CLI |
| `tests/` | Format, values, compiler, JSON, VM, host, concurrency, and CLI tests |

The physical ABI is documented in [docs/BYTECODE.md](docs/BYTECODE.md).
The complete source syntax is specified in [docs/GRAMMAR.ebnf](docs/GRAMMAR.ebnf).

## Project status

DEUS is experimental, but the repository already provides a usable vertical
slice:

- native `check`, `run`, `build`, and `exec` workflow;
- typed locals and recursive pure expressions;
- records, lists, optional access, and JSON output;
- host-backed crawling, extraction, futures, retries, and global rate limiting;
- versioned bytecode and host ABIs;
- strict `/W4 /WX` release build;
- eight automated test targets passing on the Windows reference build.

Current v1 gaps:

- compound JSON extraction;
- richer HTML selectors and document metadata;
- per-domain rate limits, redirect policy, cancellation, and `robots.txt`;
- structured partial failures for concurrent retrieval;
- packaged VS Code extension and formatter;
- signed and distributed Windows binaries;
- candidate filtering, provenance, scoring, and ranking pipelines.

The next language phase is retrieval-native composition: candidates, filters,
scoring, ranking, and provenance. General-purpose operating-system features remain
deliberately out of scope.

## Bytecode compatibility

`.deusb` uses a fixed 40-byte header, little-endian operands, interned UTF-8
strings, bounded sections, and a payload CRC32. The current physical ABI is v1.
Additive opcodes remain ABI v1 while older programs stay valid and decoding stays
unambiguous. Incompatible physical changes require a new ABI.
