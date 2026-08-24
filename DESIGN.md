# DEUS Design Constitution

## Identity

DEUS is a specialized language for building safe information-oriented systems:
acquisition, retrieval, interpretation, transformation, decision, and controlled
distribution. Search is its founding domain, not its maximum boundary. DEUS is
not a search engine by itself and is not a general-purpose programming language.

A DEUS program declares intent, sources, extraction, composition, limits, and
provenance. A compatible host supplies the capabilities that perform real work:
network access, indexes, storage, models, embeddings, or media decoding.

```text
DEUS source -> compiler -> validated bytecode -> VM -> authorized host capabilities
```

## Admission rule

A feature belongs in the language only when it directly improves acquisition,
retrieval, interpretation, transformation, decision, ranking, explanation, or
controlled distribution of information. General operating-system convenience is
not sufficient justification.

Growth is intentional, but every new paradigm must preserve bounded execution,
typed information flow, explicit authority, deterministic failure, and a clear
relationship to information systems.

Language primitives must have bounded resource behavior, explicit types,
observable effects, and a provenance-preserving interpretation.

## Responsibilities

### Language

- source and adapter declarations;
- typed information-flow expressions;
- candidates, evidence, provenance, filtering, scoring, and ranking;
- explicit effects and resource budgets;
- deterministic ordering and structured partial failure.

### VM

- bytecode validation and type enforcement;
- memory, instruction, timeout, concurrency, and response limits;
- capability checks and module allowlists;
- ownership across stack, locals, futures, and host boundaries;
- deterministic orchestration of granted effects.

### Host

- HTTP transports, credentials, proxies, and policy;
- search and vector indexes;
- model and embedding providers;
- caches and persistent storage;
- media decoders and environment-specific integrations.

The reference WinHTTP implementation is a native host, not part of the language
definition.

## Deliberate exclusions

DEUS does not provide arbitrary filesystem access, process execution, dynamic
libraries, unrestricted FFI, raw threads, pointers, reflection, unbounded loops,
or native-code packages. A host may expose a domain capability only through a
versioned, typed, limited interface.

## Host contract

Hosts declare capabilities before execution. Callbacks may be invoked concurrently
by `FORK`, so a host that grants network access must make its `hunt` callback
thread-safe. Successful callbacks return borrowed documents; the VM copies data
within its own response and memory limits, then invokes `release_document` exactly
once when supplied. Failed callbacks retain responsibility for their own cleanup.

Capabilities are authority, not discovery. A module name cannot grant an effect
that the host did not explicitly authorize.

Operational expressions such as `bind page = hunt source` describe effects in the
language AST, but their execution remains delegated to an authorized host. Pure
expressions and host effects must remain distinguishable to semantic analysis.

Dynamic URLs are domain-specific templates, not general string evaluation. The
compiler resolves typed placeholders and lowers them to bounded URL assembly;
the VM percent-encodes dynamic components before requesting the host effect.

Pure extraction such as bounded JSON path evaluation belongs to the VM because it
has deterministic semantics and no environmental authority. Network transport and
credentials remain host effects. Compound JSON values must enter the shared value
system rather than bypassing its ownership and memory limits.

The host ABI is versioned independently from the DEUSB bytecode ABI.

## Versioning

Language milestones, product releases, and the physical DEUSB ABI are independent.
Additive opcodes may remain ABI v1 when old bytecode stays valid and decoding is
unambiguous. Incompatible physical changes require a new ABI and explicit rejection
by older readers.

Source syntax may evolve independently from bytecode. Modern structured source
forms should lower to existing validated primitives when their semantics already
exist. Compatibility syntax must have an explicit migration path rather than
becoming an accidental permanent dialect.
