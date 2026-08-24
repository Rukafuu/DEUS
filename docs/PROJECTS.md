# Projects, modules and dependencies

`deus init` creates a project rooted at `deus.toml`. Version 0.1 recognizes the
manifest as a product contract; source execution still uses an explicit path.

```toml
[package]
name = "catalog-search"
version = "0.1.0"
entry = "src/main.deus"

[capabilities]
network = true

[dependencies]
# normalization = { path = "../normalization" }
```

## Module direction

The module system will distinguish pure libraries from host capabilities:

```text
deus/core          pure and deterministic
deus/text          pure and deterministic
deus/collections   pure and deterministic
deus/ranking       pure and deterministic
deus/url           pure and deterministic

net.http           host capability
index.search       host capability
embed.vector       host capability
media.decode       host capability
```

A dependency can provide code; it cannot grant authority. Capabilities remain
an explicit agreement between manifest, bytecode validation and host policy.

The staged dependency plan is local/path dependencies, manifest validation,
`deus.lock`, Git dependencies, an official package index, and only then a public
registry if demand justifies its security and governance cost. No registry or
version resolver is implied by the current manifest.
