# RFC-0003: Structured access syntax

- Status: Accepted and implemented
- Scope: pure expression access syntax
- Source compatibility: additive
- Bytecode ABI impact: none
- Host ABI impact: none

## Purpose

DEUS already has bounded, validated record and list reads through `get` and `at`.
This RFC admits expression syntax for those same operations, making rule-like
programs readable without adding branching, iteration, mutation, or ambient
host authority.

## Surface syntax

```deus
bind item = {"title": "Frieren", "scores": [95, 88]}
bind title = item.title
bind first_score = item.scores[0]
```

A member access is `expression . identifier`. An item access is
`expression [ unsigned-integer-literal ]`. Both associate left-to-right and
bind tighter than arithmetic, comparisons, conversions, and coalescing.
Chaining is allowed: `catalog.results[0].title`.

This increment does not introduce optional punctuation (`?.` or `?[...]`).
Existing `get?` and `at?` remain the explicit optional-read forms until an RFC
defines their expression equivalents without lexical ambiguity.

## Static and runtime contract

- `.field` accepts a statically known `Record` or dynamic `Value`.
- `[index]` accepts a statically known `List` or dynamic `Value`; the index is
  a non-negative compile-time integer literal within `u32` range.
- Both expressions produce dynamic `Value`; callers narrow them with `text`,
  `i64`, or `bool` before typed operations.
- A known incompatible base is a compile-time error.
- For dynamic values, member and item access use the existing mandatory runtime
  semantics: an incompatible kind, missing field, or out-of-range index fails.

## Lowering

The source forms are aliases for existing bytecode instructions:

```text
object.field -> <object> RECORD_GET "field"
items[3]     -> <items> LIST_AT 3
```

No opcodes, container layout, or ABI version change. The bytecode verifier and
both VMs retain ownership of runtime failure behavior.

## Non-goals

- Dynamic index expressions and slices.
- Optional expression access.
- Record shape inference, typed collection elements, iteration, or sorting.
- Mutation syntax, loops, conditionals, or general-purpose control flow.

## Acceptance criteria

1. Chained member/item expressions parse with postfix precedence.
2. Lowering uses `RECORD_GET` and `LIST_AT` only.
3. Known String, I64, Bool, Null, Document, and Future bases are rejected.
4. Non-literal, negative, or oversized item indexes are rejected.
5. Existing `get`, `get?`, `at`, and `at?` behavior remains unchanged.
6. Grammar, language tour/specification, formatter/LSP material, and release
   notes describe the feature consistently.