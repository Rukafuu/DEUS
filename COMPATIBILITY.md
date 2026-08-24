# Compatibility Policy

DEUS is an open source project in the experimental 0.x stage. Compatibility is
best effort unless a document explicitly says otherwise.

## Source language

Source syntax may evolve independently from bytecode. Legacy syntax and newer
structured forms are documented in the repository grammar and examples; no
perpetual source compatibility promise is made during 0.x.

## Bytecode ABI

The physical `.deusb` format is ABI v1 and is documented in
[docs/BYTECODE.md](docs/BYTECODE.md). Additive opcodes can remain ABI v1 when
older bytecode remains valid and decoding stays unambiguous. An incompatible
physical encoding change requires a new ABI and explicit rejection by older
readers.

## Host ABI

The host ABI is versioned independently from the bytecode ABI. Hosts explicitly
declare capabilities, and a module name cannot grant an undeclared effect.

## CLI and VS Code extension

The native CLI and VS Code extension are versioned project components, but no
stable compatibility guarantee is made during 0.x. The extension invokes the
CLI for formatting and diagnostics; see
[editors/vscode/README.md](editors/vscode/README.md).

Compatibility decisions should preserve bounded execution, typed information
flow, explicit authority, deterministic failure, and clear migration paths.
