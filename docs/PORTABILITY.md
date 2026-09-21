# Platform support

DEUS separates portable language tooling from environmental hosts.

| Component | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Compiler and bytecode builder | Supported | Supported | Supported |
| `check`, `fmt`, `build`, `init` | Supported | Supported | Supported |
| Language server | Supported | Supported | Supported |
| VM and `run`/`exec` | WinHTTP async | Portable synchronous | Portable synchronous |
| Built-in WinHTTP host | Supported | Not applicable | Not applicable |
| Embedder-provided `DeusHost` | Supported | Supported | Supported |

On Linux and macOS, CMake builds `deusvm` and links `deus run`/`exec` to the
portable synchronous VM. Pure programs need no host. Retrieval is capability-
gated through the versioned `DeusHost` ABI; without a host, network opcodes fail
before environmental access. `FORK`, `AWAIT` and `JOIN` preserve their bytecode
semantics with synchronous fallback execution.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The portable build installs `deus`, `deusc`, `deusvm` and
`deus-language-server`. Native Ubuntu and macOS builds are exercised by
`.github/workflows/portable-runtime.yml`.

## Runtime boundary

Environmental integrations remain separate hosts:

- WinHTTP and Windows Thread Pool on Windows;
- embedder-provided bounded `DeusHost` retrieval on Linux and macOS;
- the same versioned `DeusHost` capability ABI on every platform.

Portable hosts must preserve the existing response, retry, ownership and
capability limits. Portability is not permission to weaken the design
constitution.
