# Cookbook

## Validate in CI

```powershell
deus fmt --check src/main.deus
deus check src/main.deus
```

## Use a dynamic URL safely

```deus
genesis
bind query = "bounded retrieval"
bind page = hunt "https://example.test/search?q={query}"
halt
```

Typed placeholders are percent-encoded by the VM; they are not general string
evaluation.

## Read optional data

```deus
genesis
bind result = {"title": "Frieren"}
bind subtitle = get? result "subtitle"
bind display = subtitle ?? "Untitled"
load display
emit
halt
```

## Build a reproducible artifact

```powershell
deus check src/main.deus
deus build src/main.deus -o target/app.deusb
deus exec target/app.deusb
```

The physical bytecode contract is documented in [BYTECODE.md](BYTECODE.md).
