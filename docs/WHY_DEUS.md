# Why DEUS instead of Python plus requests?

Python is the better choice for general automation. DEUS exists for retrieval
flows whose execution contract must be inspectable and bounded before they run.

DEUS provides:

- validated, versioned bytecode rather than arbitrary native execution;
- explicit resource, concurrency and response limits;
- immutable typed values and deterministic composition;
- host-granted capabilities instead of ambient filesystem, process or network
  authority;
- provenance-oriented retrieval primitives and reproducible artifacts;
- a small language surface that can be audited independently of its host.

DEUS deliberately excludes arbitrary process execution, unrestricted FFI,
dynamic libraries, raw threads and general filesystem access. It is a language
for safe information acquisition pipelines, not a replacement for a
general-purpose language.
