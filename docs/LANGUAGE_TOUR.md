# Language Tour

A DEUS file is a top-level information flow. `genesis` begins the executable
program and `halt` ends it.

```deus
genesis
bind score = 95
bind accepted = score >= 80
load accepted
emit
halt
```

`bind` creates an immutable typed local. DEUS supports `Null`, `Bool`, `I64`,
`String`, lists, records, documents, futures and structured errors. Pure
expressions cannot perform host effects.

```deus
bind candidate = {"title": "Frieren", "scores": [95, 88]}
bind title = candidate.title
bind first_score = candidate.scores[0]
bind subtitle = get? candidate "subtitle"
bind label = subtitle ?? "Untitled"
```

Retrieval is explicit and capability-bound:

```deus
omni "net.http2"
genesis
limit 4
bind page = hunt "https://example.test/search"
bind title = reap page "h1"
load title
emit
halt
```

Naming a module does not grant authority. The host must authorize the network
capability. See [Why DEUS?](WHY_DEUS.md) for the design rationale and
[GRAMMAR.ebnf](GRAMMAR.ebnf) for the complete syntax.
