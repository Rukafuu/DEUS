# Projects, modules and dependencies

`deus init` creates a project rooted at `deus.toml`. `deus run`, `deus check`
and `deus build` accept a source file, a project directory, or the path to its
`deus.toml` manifest. A project build without `-o` writes
`target/<package>.deusb`.

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

Version 0.1 validates local/path dependencies and writes a deterministic
`deus.lock` when a project command runs. Each dependency path must contain a
valid `deus.toml`, and its package name must match the dependency key. Remote,
Git and registry dependencies are intentionally unsupported. A dependency can
declare code, but never grants a capability to the application.
