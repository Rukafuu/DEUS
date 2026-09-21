# Release notes

## 0.4.0 - 2026-08-31

### Static typing

- Boolean and ordering operators now require statically known Bool and I64 values. Convert adapter or JSON values explicitly before using them in logic.
- `emit` and `debug` reject known `Document` values during compilation.

### Structured access syntax

- Records and lists can now be read inside pure expressions as `record.field` and `list[0]` (including chains). These are aliases for the existing mandatory `get` and `at` operations.

### Checked I64 arithmetic

- `+`, `-`, `*`, `/`, and `%` compile only for statically known I64 operands.
- Arithmetic detects overflow; division and modulo reject zero divisors. Division truncates toward zero.

### Diagnostics

- `debug` writes a serializable top-of-stack value to stderr, leaving stdout reserved for `emit` output.

### Host adapter results

- The language supports `bind output = call "adapter.operation" input` through the host adapter capability.
- Adapter results retain the dynamic `Value` type and may be refined with `get` or `at` when the runtime result is a record or list.
- `get?` and `at?` now return `null` for a missing key/index or a dynamic value with an incompatible kind. Mandatory reads keep failing in those cases.

### Compatibility

- Bytecode ABI is version 4 because it adds the checked I64 arithmetic opcodes. Host ABI remains version 2. Recompile DEUS programs after upgrading from ABI v3.
- A host must explicitly advertise `DEUS_HOST_CAP_ADAPTER_CALL`; no ambient network, filesystem, model, or database access is granted by the language.
