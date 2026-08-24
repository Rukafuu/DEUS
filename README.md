# DEUS: Decoupled Extended Unitary Script

<p align="center">
  <img src="assets/deus-logo.png" alt="DEUS language logo" width="260">
</p>

[![Platforms](https://img.shields.io/badge/Tooling-Windows%20%7C%20Linux%20%7C%20macOS-0078D4)](docs/PORTABILITY.md)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake)](https://cmake.org/)
[![C17](https://img.shields.io/badge/Implementation-C17-00599C?logo=c)](https://en.wikipedia.org/wiki/C17_(C_standard_revision))
[![ABI](https://img.shields.io/badge/Bytecode_ABI-v1-8A2BE2)](docs/BYTECODE.md)
[![Status](https://img.shields.io/badge/Status-Experimental-FF6B6B)](#open-source-and-community)

DEUS is an experimental language for describing safe information acquisition,
retrieval, extraction, and composition pipelines. It is not a general-purpose
language and it is not a search engine by itself.

A DEUS program declares intent. The compiler validates that intent, the VM
enforces types and resource limits, and an authorized host performs environmental
work such as HTTP requests, index access, storage, embeddings, or media decoding.

```text
DEUS source → compiler → validated bytecode → bounded VM → authorized host
```

The reference implementation is native C17. Language tooling can be built on
Windows, Linux and macOS; the reference VM and WinHTTP host remain Windows-only.
See the [platform support matrix](docs/PORTABILITY.md).

## A DEUS program in 30 seconds

```deus
genesis

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
halt
```

DEUS supports typed locals, structured values, optional reads, pure expressions,
and compact JSON output. `emit` writes values directly without adding separators
or newlines, leaving output framing to the program or host.

## Build and run

Requirements for the complete Windows runtime:

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

The official logo is installed under `share/deus/deus-logo.png`. The source asset
is kept in `assets/deus-logo.png`.

### VS Code extension

The language support under `editors/vscode` is a packageable extension with the
official logo, `.deus` file association, syntax highlighting, comments, brackets,
and auto-closing pairs:

```powershell
cd editors\vscode
npx @vscode/vsce package
code --install-extension deus-language-0.1.0.vsix
s capabilities and may currently provide `hunt` through
`DEUS_HOST_CAP_NETWORK`. Returned documents are borrowed: the VM copies them under
its 32 MiB response limit and invokes `release_document` exactly once. Host
callbacks used by `FORK` must be thread-safe.

`deus_vm_execute_program` remains the compatibility entry point and selects the
native WinHTTP reference host. A supplied host does not initialize WinHTTP.

## Value system

The foundation of the adapter language defines `null`, booleans, `i64`, UTF-8
strings, bytes, lists, records, HTTP documents, futures, and structured errors in
`include/deus_value.h`. Allocated values use shared references; `deus_value_copy`,
`deus_value_move`, and `deus_value_dispose` make ownership explicit. Futures accept a
finalizer for the native resource they encapsulate.

Each value family belongs to a `DeusValueContext`, which enforces memory budgets and
limits for nesting depth, items, fields, and blobs. Composite values reject cycles
and cross-context references, preventing refcount cycles and ambiguous lifetimes.
The context must live until all of its values are discarded.

This module does not change the ABI v1 or add opcodes. The existing VM remains
compatible while the new AST and semantic analysis pipeline is introduced in later
increments.

## Compiler pipeline

`deus_parse_source` is a compatibility facade over independent stages:

```text
source → lexer → parser → AST → semantic analysis → ABI v1 bytecode generation
```

Tokens carry line and column information; AST nodes have typed operands and their own
string ownership. Stack effect analysis, executor limits, and interning happen only
after parsing. Unknown instructions may include a short suggestion without altering
the diagnostic format consumed by the CLI.

## Binary alphabet — ABI v1

| Byte | Mnemonic | Operand | Stack effect |
| --- | --- | --- | --- |
| `0x01` | `OMNI` | `u32` index | loads module |
| `0x02` | `GENESIS` | — | starts execution |
| `0x03` | `HUNT` | `u32` URL index | `[] → [Document]` |
| `0x04` | `REAP` | `u32` selector index | `[Document] → [Text]` |
| `0x05` | `HALT` | — | terminates immediately |
| `0x06` | `EMIT` | — | `[Text] → []` and writes to stdout |
| `0x07` | `FORK` | `u32` URL index | `[] → [Future]` |
| `0x08` | `AWAIT` | — | `[Future] → [Document]` |
| `0x09` | `JOIN` | `u32` count | `[Future × N] → [Document × N]` |
| `0x0A` | `LIMIT` | `u32` workers | sets concurrency, `1..256` |
| `0x0B` | `RETRY` | `u32` attempts | configures retries, `0..16` |
| `0x0C` | `BACKOFF` | `u32` milliseconds | exponential backoff base |
| `0x0D` | `RATE` | `u32` requests/s | global limiter; `0` disables it |
| `0x0E` | `CONST` | `u32` string index | `[] → [String]`; allocates a constant copy |
| `0x0F` | `BIND` | `u32` local slot | `[Value] → []`; moves into an empty slot |
| `0x10` | `LOAD` | `u32` local slot | `[] → [Value]`; pushes an independent copy |
| `0x11` | `CONST_NULL` | — | `[] → [Null]` |
| `0x12` | `CONST_BOOL` | `u32` (`0` or `1`) | `[] → [Bool]` |
| `0x13` | `CONST_I64` | signed `i64` | `[] → [I64]` |

All integers use little-endian encoding. The fixed header is:

```text
0x00  byte[8] magic = 44 45 55 53 42 00 01 00
0x08  u16     ABI version
0x0A  u16     flags
0x0C  u32     string count
0x10  u32     string section offset
0x14  u32     string section length
0x18  u32     bytecode offset
0x1C  u32     bytecode length
0x20  u32     payload CRC32
0x24  u32     reserved
```

Each string is `u32 length + UTF-8 bytes`. The VM validates the magic, ABI, offsets,
limits, CRC32, opcodes, and indices before execution begins.

`CONST`, `BIND`, `LOAD`, and the scalar constants are additive ABI v1 instructions. Current readers
still accept every earlier v1 program. Programs may define at most 256 locals;
`BIND` transfers ownership and `LOAD` creates an independent copy. Uninitialized
slots and rebinding are rejected.

## Syntax

```deus
omni "net.http2"
genesis
limit 8
retry 3
backoff 100
rate 20
fork "https://example.com"
fork "https://example.org"
join 2
reap "h1"
emit
reap "h1"
emit
halt
```

String locals use an explicit stack-oriented form:

```deus
genesis
bind query = "frieren"
load query
emit
halt
```

`bind` accepts strings, signed `i64`, booleans, `null`, and an already-bound local.
Names have program scope, cannot be redeclared, and may only be used after
`genesis`. `EMIT` writes scalars as UTF-8 text. HTTP expressions, interpolation,
lists, and records will build on the same typed AST.

Comments begin with `#` or `//`. ABI v1 accepts simple tag selectors,
`.class`, and `#id`. The linter requires exactly one `genesis`, one terminal
`halt`, and validates stack effects, pending futures, numeric limits, and the
position of executor settings. `LIMIT`, `RETRY`, `BACKOFF`, and `RATE` must appear
before the first `FORK`. `JOIN N` preserves the original document order.

## Native Windows build

With Visual Studio Build Tools (Desktop development with C++) and CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Usage:

```powershell
.\build\Release\deusc.exe check examples\first_hunt.deus
.\build\Release\deusc.exe build examples\first_hunt.deus -o first_hunt.deusb
.\build\Release\deusvm.exe first_hunt.deusb
```

The network layer uses WinHTTP and negotiates HTTP/2 over TLS when both the server and
system support it. It includes timeouts, gzip/deflate support, rejection of non-2xx
statuses, and a 32 MiB response cap. A single `HINTERNET` handle is shared to allow
connection reuse. The executor uses the Windows Thread Pool, a concurrency semaphore,
event-driven futures, global rate limiting, and retries with exponential backoff
capped at 60 s.

## Open source and community

DEUS is an open source project with maintainer-led governance. See
[CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines,
[SUPPORT.md](SUPPORT.md) for official support through GitHub Issues, and
[GOVERNANCE.md](GOVERNANCE.md) for project decision-making.
The official project site is [deuslang.org](https://deuslang.org).

Security reports should follow [SECURITY.md](SECURITY.md) rather than a public
issue. Release planning and announcements are tracked through GitHub Issues.
The project maintainer is [Rukafuu](https://github.com/Rukafuu), with updates
at [GitHub](https://github.com/rukafuu) and
[LinkedIn](https://linkedin.com/in/rukafuu).

The source code is licensed under the [Apache License 2.0](LICENSE). The DEUS
name and official assets are covered separately by [TRADEMARKS.md](TRADEMARKS.md).
