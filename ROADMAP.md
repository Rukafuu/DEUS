# DEUS Roadmap

DEUS is the bounded language and runtime used to describe information pipelines.
The Truth owns crawling policy, persistence, indexes, ranking, APIs, and service
operation. Milestones below are integration gates, not calendar promises.

## M1 — Embeddable runtime and Host ABI

- [x] Native C17 compiler, validated DEUSB ABI v1, and embeddable VM library.
- [x] Host ABI v1 network capability with borrowed-document ownership.
- [x] Public host validation and focused compatibility test.
- [x] Output sink independent from `FILE *` and process stdout.
- [x] Cooperative execution options with cancellation, deadline, and
  instruction budget.
- [ ] Structured execution result and error reporting.
- [ ] Deterministic The Truth mock-host integration fixture.

**Gate:** The Truth loads validated bytecode, executes it in-process, captures a
structured result, cancels it safely, and observes bounded failure without
spawning a DEUS executable.

## M2 — Typed information pipelines

- [x] Typed scalar and structured values, locals, expressions, and bounded
  collection construction.
- [x] Structural source AST and `limits:` lowering without a bytecode change.
- [ ] Canonical `Optional<T>`, `List<T>`, record shapes, and typed VM stack.
- [ ] Immutable or copy-on-write collection ownership.
- [ ] Bounded item scopes and pipeline operations for filter, stable rank, and
  take.
- [ ] Typed URL, document extraction, provenance, and partial-failure schemas.

**Gate:** a DEUS program transforms acquired documents into deterministic,
typed index records with explicit provenance and work limits.

## M3 — The Truth vertical search slice

- [ ] Versioned The Truth capability contract for fetch, index read/write,
  storage, embeddings, and metrics.
- [ ] Controlled crawler frontier, robots policy, deduplication, and local
  document store in The Truth.
- [ ] Inverted index and BM25 baseline in The Truth.
- [ ] End-to-end corpus: acquire, extract, index, query, rank, and explain.
- [ ] Relevance, latency, memory, and failure benchmarks.

**Gate:** a controlled source collection can be rebuilt and queried locally,
with DEUS authoring the acquisition/transformation pipeline and The Truth owning
the engine.

## M4 — Production hardening

- [ ] Linux reference host and cross-platform embedding contract.
- [ ] Fuzzing for source, bytecode, host documents, JSON, and HTML.
- [ ] SSRF, redirect, decompression, credential, and capability policies.
- [ ] Backpressure, isolation, observability, signed releases, and ABI migration
  tests.
- [ ] Hybrid lexical/vector retrieval and incremental indexing.

**Gate:** repeatable releases meet documented security, compatibility,
relevance, and operational targets.

## Current focus

Finish M1 in order: structured results, then the The Truth mock-host fixture.
Do not expand Host ABI v1 or add search-engine
storage/ranking opcodes to DEUS merely to mirror engine internals.
