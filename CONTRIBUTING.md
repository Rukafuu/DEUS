# Contributing to DEUS

DEUS is an open source, specialized language for information acquisition, retrieval,
extraction, composition, and search pipelines. Contributions should preserve
bounded execution, typed information flow, explicit host capabilities, and
bytecode compatibility.

## Environment

The current reference implementation supports Windows. Install Visual Studio
2022 Build Tools with **Desktop development with C++**, and CMake 3.20 or later.
From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Strict builds enable `/W4 /WX` under MSVC. All existing tests should continue to
pass.

## Workflow

1. Fork the repository and create a focused branch.
2. Make the smallest coherent change.
3. Build and run the tests locally.
4. Update documentation when public behavior or language rules change.
5. Open a pull request with the relevant context.

The current project maintainer is [reskyume](https://github.com/reskyume).

Avoid unrelated refactors and feature dumping. Changes to grammar, syntax,
semantics, bytecode, ABI, VM behavior, or host capabilities need architectural
justification and, when appropriate, an RFC. Consult [DESIGN.md](DESIGN.md).

## Tests and documentation

Add or update focused tests for behavioral changes. Language changes should
update the relevant reference, grammar, examples, and VS Code tooling when
needed. ABI changes must explain compatibility and versioning impact.

## Developer Certificate of Origin

Contributions must include a DCO sign-off:

```text
Signed-off-by: Name <email>
```

Use `git commit -s` to add it automatically. By signing off, you certify that
you have the right to submit the contribution under its project license and
agree to the Developer Certificate of Origin.
