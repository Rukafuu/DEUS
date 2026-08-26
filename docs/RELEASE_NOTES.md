# Release notes

## Unreleased

### Host adapter results

- The language supports `bind output = call "adapter.operation" input` through the host adapter capability.
- Adapter results retain the dynamic `Value` type and may be refined with `get` or `at` when the runtime result is a record or list.
- `get?` and `at?` now return `null` for a missing key/index or a dynamic value with an incompatible kind. Mandatory reads keep failing in those cases.

### Compatibility

- Bytecode ABI and host ABI are version 2. Recompile DEUS programs and update host embeddings together when moving from ABI v1.
- A host must explicitly advertise `DEUS_HOST_CAP_ADAPTER_CALL`; no ambient network, filesystem, model, or database access is granted by the language.