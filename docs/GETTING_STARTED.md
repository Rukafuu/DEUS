# Getting Started

DEUS turns bounded retrieval programs into validated bytecode executed by a
capability-aware VM. You do not need to understand the bytecode format to write
your first program.

## Create a project

After installing `deus.exe`, open PowerShell and run:

```powershell
deus init hello
cd hello
deus run src/main.deus
```

The generated `deus.toml` declares the entry file and requested capabilities.
The minimal template requests no environmental capability.

Other starting points are available:

```powershell
deus init catalog --template crawler
deus init scoring --template ranking
```

The crawler template requests network authority in its manifest. The host still
decides whether to grant that authority at execution time.

## Everyday commands

```text
deus check src/main.deus       validate without executing
deus run src/main.deus         compile and execute in memory
deus fmt src/main.deus         format a valid source file
deus fmt --check src/main.deus check formatting without writing
deus build src/main.deus       produce src/main.deusb
deus exec src/main.deusb       execute validated bytecode
```

Continue with the [Language Tour](LANGUAGE_TOUR.md), then use the
[Cookbook](COOKBOOK.md) for task-oriented examples.
