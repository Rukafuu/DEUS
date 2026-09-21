# RFC-NEXT: Structured retrieval language evolution

- Status: Accepted and implemented
- Investigation completed: 2026-08-24
- First increment selected: `limits:`
- Source compatibility: additive
- Bytecode ABI impact of first increment: none
- Host ABI impact of first increment: none
- Implementation completed: 2026-08-24

## Purpose

This RFC is the contract for the next language increment after indented flows.
It records the language, parser, type-system, VM/ABI, retrieval-domain, and
tooling investigations that selected `limits:` as the first structural block.
The increment changes the compiler frontend, formatter, documentation, and
editor support while deliberately leaving the VM and both ABIs unchanged.

DEUS remains a specialized language for bounded information acquisition,
retrieval, extraction, composition, filtering, evaluation, ranking, and
explanation. New syntax must express those concerns more clearly; it must not
become general-purpose control flow with search-themed names.

## Current architectural facts

The current modern frontend is:

```text
source
-> structural Source AST + explicit layout events
-> validated lowering to the Core AST
-> flat instruction AST + recursive Expression AST
-> semantic analysis
-> ABI v1 bytecode
-> bounded VM and authorized host
```

The investigation established the following constraints:

1. The modern frontend materializes `LINE`, `INDENT`, `DEDENT`, and `EOF`
   events in a Source AST before Core parsing.
2. Source spans are retained across structural validation and lowering.
3. `limits:` is represented structurally and lowers to the existing
   `LIMIT`/`RETRY`/`BACKOFF`/`RATE` Core instructions.
4. Core structured literals still desugar into hidden locals and mutations;
   fragment parsing shares a hidden-symbol base so separate fragments cannot
   collide.
5. Pure expressions are limited to one physical line and currently lack
   arithmetic, member access, parameters, and item scopes.
6. Semantic collection types are only `List` and `Record`; element and record
   field types are not retained.
7. `LOCAL_SCALAR` and `LOCAL_VALUE` are permissive union-like states. Some
   invalid operations are therefore rejected only by the VM.
8. The analyzer tracks stack depth and a future count, not a typed abstract
   stack. It does not currently prove that `emit` receives a serializable value.
9. The bytecode has no collection iteration, predicate execution, branching,
   stable sorting, slicing, or arithmetic for scoring.
10. The CLI formatter is not aware of `flow` ownership. It can remove the
    required body indentation, so formatter round-trip safety is a prerequisite
    for adding another indented construct.

These are engineering facts, not reasons to abandon the planned language
direction. They determine the order in which that direction can be implemented
honestly.

## Decision matrix

`ACCEPT` means admitted to the language direction. Only the selected first
increment may be implemented from this RFC. `DEFER`, `REJECT`, and `EXPERIMENT`
must receive a later RFC and may not be implemented in this increment.

| Feature | Value | Complexity | ABI impact | Search relevance | Recommendation |
| --- | ---: | ---: | ---: | ---: | --- |
| `limits:` | High | Low after layout foundation | None | High | **ACCEPT — first increment** |
| `parallel:` | High | High | None only for constant hunts | High | **DEFER** |
| `pipeline` | Very high | Very high | Collection capability needed | Very high | **DEFER** |
| `filter` | High | High | Collection capability needed | Very high | **DEFER** |
| `rank` | Very high | High | Collection capability needed | Very high | **DEFER** |
| `take` | High | Medium | Collection capability needed | High | **DEFER** |
| `rule` | Medium | High | None if bounded inline | Medium | **EXPERIMENT** |
| provenance schema | Very high | Medium | None for explicit records | Very high | **EXPERIMENT** |
| provenance keyword | Unclear | High | Host/value contract likely | High | **REJECT for now** |
| special `score` syntax | Low today | Medium | Arithmetic missing | High | **REJECT for now** |
| conditional statements | Low | High | Branching required | Low | **REJECT** |

## Selected increment: `limits:`

### Why this syntax exists

Executor configuration is currently expressed as unrelated flat instructions:

```deus
limit 8
retry 3
backoff 100
rate 20
```

These values form one retrieval policy. The block makes that ownership visible,
distinguishes worker concurrency from future result truncation, and keeps
resource bounds prominent without adding runtime authority or general control
flow.

```deus
flow search:
    limits:
        workers 8
        retry 3
        backoff 100
        rate 20

    bind page = hunt "https://example.test"
```

`workers` is the source-level name for the existing `LIMIT` machine primitive.
`backoff` is measured in milliseconds and `rate` in requests per second.

### Source contract

```ebnf
flow_decl        = "flow" , identifier , ":" , NEWLINE ,
                   INDENT , flow_item , { flow_item } , DEDENT ;

flow_item        = simple_statement | limits_block ;

limits_block     = "limits" , ":" , NEWLINE ,
                   INDENT , limit_entry , { limit_entry } , DEDENT ;

limit_entry      = limit_name , unsigned_integer , NEWLINE ;

limit_name       = "workers" | "retry" | "backoff" | "rate" ;
```

Phase-one rules:

- at most one `limits:` block per flow;
- it must precede the first network effect;
- entries are compile-time unsigned integer literals;
- entries may appear in any order but may not be duplicated;
- unknown entries are errors;
- `workers` is required to be `1..256`;
- `retry` is required to be `0..16`;
- `backoff` is required to be `0..60000` milliseconds;
- `rate` is required to be `0..10000` requests per second;
- the block has no runtime scope and produces no value;
- legacy `limit`, `retry`, `backoff`, and `rate` statements remain accepted
  during migration, but mixing a legacy executor setting with `limits:` is an
  error rather than a last-write-wins rule.

### Source AST and lowering

The source representation is structural:

```text
FlowDecl
└── items[]
    └── LimitsBlock
        └── entries[]
            ├── kind
            ├── U32 literal
            └── source span
```

After structural and semantic validation, lowering is exact:

```text
workers N -> LIMIT N
retry N   -> RETRY N
backoff N -> BACKOFF N
rate N    -> RATE N
```

No `OP_LIMITS` is permitted. Generated bytecode must be byte-for-byte equivalent
to the corresponding valid legacy settings in canonical entry order.

### Diagnostics contract

The first implementation must provide targeted messages for:

```text
duplicate `retry` entry in `limits`
unknown limit `workerss`; did you mean `workers`?
`workers` must be between 1 and 256; found 0
`limits` must appear before the first network operation
expected an indented limits entry
`retry` cannot appear both in `limits` and as a legacy setting
```

New structural nodes must retain start and end spans. Diagnostic codes and JSON
output are desirable tooling foundations, but they are not required to ship this
single block unless introduced consistently across existing diagnostics.

### Tooling contract

Before `limits:` is considered complete:

- the formatter must understand semantic block depth;
- semantic indentation is four spaces per block;
- literal continuation indentation must compose with block indentation;
- tabs remain invalid in leading indentation;
- comments retain their block owner;
- formatting is idempotent;
- formatted source parses to the same semantics;
- `fmt --check` passes all canonical modern examples;
- TextMate highlighting treats `limits` as a block header and its entries
  contextually, rather than adding every entry to a global keyword expression;
- the extension supplies a `limits` snippet;
- the old syntax remains highlighted during migration.

## Required frontend foundation

The implementation must not grow `deus_layout_lower` into a keyword-specific
text rewriter. It must establish a bounded structural source layer:

```text
source
-> logical lines and INDENT/DEDENT events
-> structured Source AST
-> semantic validation
-> existing Core AST / instructions
-> ABI v1 bytecode
```

Indent changes are suspended inside `{}`, `[]`, and future parenthesized
multiline expressions. The legacy flat parser may remain as a compatibility
path. The modern path must preserve original source spans through lowering.

The minimum structural scope for this increment is `FlowDecl`, ordinary flow
items, and `LimitsBlock`; it is not authorization to add general nested scopes.

## Deferred retrieval pipeline direction

The accepted design direction, not current syntax, is an immutable expression
with explicit result and item ownership:

```deus
bind results = pipeline candidates as candidate:
    filter candidate.score >= 80
    rank candidate.score desc
    take 10
```

The shorter form `pipeline candidates:` is rejected because it hides whether
the input is mutated, where the result lives, and what name represents the
current item.

Before a pipeline RFC may be accepted, DEUS needs:

- `List<T>` and useful record-field typing;
- member access or an equally readable typed field-access form;
- pure item scopes without general closures;
- an explicit collection-work budget;
- immutable or copy-on-write collection ownership;
- stable deterministic ranking and tie policy;
- explicit `Null` and error behavior;
- a VM/core capability for bounded collection processing.

`filter`, `rank`, and `take` are reserved as pipeline stages only. They are not
general statements. No `OP_PIPELINE`, `OP_FILTER`, `OP_RANK`, or `OP_TAKE` may
be added merely to mirror source keywords.

## Deferred structured retrieval concurrency

`parallel:` is useful only as structured retrieval fan-out, never as raw async
control flow. A future minimum subset may contain independent named constant-URL
hunts and lower to `FORK`, `JOIN`, and reverse-order binds.

It remains deferred until a dedicated RFC fixes:

- lexical result ordering;
- all-or-nothing versus partial failure;
- cancellation and teardown;
- inherited worker budgets;
- dynamic URL behavior;
- prohibition of sibling dependencies and escaping futures;
- interaction with typed `Error` results.

No `OP_PARALLEL` is planned.

## Rules, scoring, and provenance

### Rules

`rule` remains experimental. If admitted, it must take an explicit subject,
contain one pure Boolean expression, prohibit recursion, and lower through
bounded inline expansion. A capture-based `rule eligible:` is rejected.

### Scoring

There is no special `score` statement. Weighted scoring should first use checked
`I64` arithmetic in the Expression AST. Arithmetic requires its own small RFC,
including deterministic overflow failure. A future scoring construct is justified
only if it records named ranking contributions or other explanation semantics.

### Provenance

Provenance is a domain obligation, but not yet a keyword. The first representation
should be an explicit, typed, serializable schema. Host effects create acquisition
claims; pure VM transformations preserve or derive them within fixed bounds.

A record merely named `provenance` is not trusted provenance. Authenticated or
host-attested provenance requires a versioned host/value contract. Extending the
physical `DeusHost` struct under ABI v1 is not permitted.

## Existing safety work discovered by the investigation

These findings are not part of the selected syntax increment, but they block
honest pipelines and should receive focused RFCs or issues:

1. Define a canonical compiler type model with `Optional<T>`, `List<T>`, record
   shape information, effects, and traits such as `Serializable` and `Orderable`.
2. Make `deus check` reject `emit` of `Document`, `Future`, `Bytes` without an
   explicit format, and other non-serializable values.
3. Validate external JSON at the boundary through a typed extraction design.
4. Replace permissive `LOCAL_SCALAR`/`LOCAL_VALUE` operator acceptance with
   explicit narrowing or conversion.
5. Track a typed abstract stack during semantic generation.
6. Resolve collection aliasing: `LOAD` is a shallow reference-counted copy while
   `set` and `push` mutate shared objects. Choose freeze-after-bind, linear
   builders, or copy-on-write before collection pipelines.
7. Establish a global work budget before repeated per-item evaluation.

## Implementation plan for the selected increment

Implementation is a later round. File ownership must remain disjoint while work
is concurrent.

1. **Lead / Integrator**
   - freeze this RFC contract;
   - define Source AST interfaces and compatibility boundaries;
   - integrate in order and reject unrelated refactors.
2. **Layout and parser implementation**
   - own layout scanner, Source AST, parser integration, and grammar tests;
   - produce structural flow and limits nodes with spans;
   - leave runtime decisions to semantics.
3. **Semantic and lowering implementation**
   - validate uniqueness, placement, literal values, bounds, and migration
     conflicts;
   - lower to existing executor opcodes only;
   - do not modify the VM or ABI.
4. **Tooling implementation**
   - make formatting layout-aware and idempotent;
   - update TextMate grammar, indentation behavior, snippet, and changelog.
5. **Adversarial tests**
   - valid order variations and partial blocks;
   - duplicate, unknown, missing, negative, overflow, and out-of-range entries;
   - malformed indent/dedent, tabs, comments, blank lines, and CRLF;
   - placement after network effects;
   - mixed legacy/modern configuration;
   - bytecode equivalence with legacy source;
   - formatter parse round-trip and idempotence;
   - regression of flat source and old bytecode.
6. **Language Red Team**
   - review ambiguity, unbounded behavior, compatibility, diagnostics, source
     mapping, formatter safety, and accidental general-purpose scope;
   - recommend rollback if the structural frontend is not sound.
7. **Lead validation**
   - strict `/W4 /WX` build;
   - all existing and new tests;
   - canonical examples through `check`, `run`, and `fmt --check`;
   - update README, grammar, this RFC, examples, extension changelog, and
     bytecode documentation only if the physical contract actually changes.

## Completion gate

The `limits:` increment is complete only when:

- modern and legacy programs remain accepted as specified;
- generated executor bytecode is equivalent to the legacy form;
- no VM, bytecode ABI, or host ABI changes were introduced;
- formatter round-trip and idempotence tests pass;
- all diagnostics identify the correct source span;
- `/W4 /WX` is clean;
- the full test suite passes;
- documentation and editor support ship in the same change.

DEUS should grow muscular, not swollen: one domain block, one precise semantic
contract, and no machine primitive that the language does not fundamentally need.
