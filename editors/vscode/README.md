<h1 align="center">DEUS Language for Visual Studio Code</h1>

<p align="center">Native editor tooling for safe, bounded information-retrieval programs.</p>

<p align="center">
  <img alt="Version 0.4.0" src="https://img.shields.io/badge/version-0.4.0-6f42c1">
  <img alt="Visual Studio Code 1.85 or newer" src="https://img.shields.io/badge/VS%20Code-%5E1.85.0-007ACC?logo=visualstudiocode&logoColor=white"><br>
  <img alt="DEUS files" src="https://img.shields.io/badge/language-.deus-d4af37">
  <img alt="Native language server" src="https://img.shields.io/badge/tooling-native%20LSP-2ea44f"><br>
  <img alt="Experimental status" src="https://img.shields.io/badge/status-experimental-f97316">
</p>

DEUS is a specialized language for expressing information acquisition,
retrieval, extraction, and composition pipelines. This extension brings the
official `.deus` editing experience to VS Code without hiding what runs on your machine.

```deus
flow main:
    bind result = {
        "title": "Frieren",
        "year": 2023,
        "meta": {"verified": true}
    }

    bind title = get result "title"
    bind subtitle = get? result "subtitle"
    bind display = subtitle ?? "Untitled"

    load result
    emit
```

## Everything your `.deus` files expect

### Read the language, not the punctuation

- syntax highlighting for modern flows and compatibility syntax;
- dedicated colors for keywords, literals, types, comments, and operators;
- language-aware comments, brackets, indentation, and auto-closing pairs;
- snippets for flows, limits, bindings, records, lists, output, and HTTP retrieval.

### Catch mistakes while they are still small

- live native-parser diagnostics while you edit;
- automatic `deus check` diagnostics when a document is opened or saved;
- **DEUS: Check Current File** for an explicit validation pass;
- document outline, keyword hover, and same-file definition lookup through the native DEUS language server.

### Keep source clean

- **Format Document** powered by `deus fmt`;
- support for unsaved editor contents;
- temporary-file isolation: formatting and checking never rewrite your source behind VS Code's back.

## Quick start

1. Install the DEUS CLI and native language server.
2. Make `deus` and `deus-language-server` available on `PATH`.
3. Install the version-matched VSIX:

   ```powershell
   code --install-extension deus-language-0.4.0.vsix
   ```

4. Open any `.deus` file. VS Code will select the **DEUS** language mode automatically.

To enable format on save:

```json
{
  "[deus]": {
    "editor.defaultFormatter": "reskyume.deus-language",
    "editor.formatOnSave": true
  }
}
```

## Extension settings

| Setting | Default | Purpose |
| --- | --- | --- |
| `deus.executablePath` | `deus` | CLI used for formatting and fallback diagnostics |
| `deus.languageServerPath` | `deus-language-server` | Native language server executable |
| `deus.diagnostics.enable` | `true` | Runs diagnostics when DEUS documents are opened or saved |

Executable paths may be absolute and are passed directly to the operating system without a shell:

```json
{
  "deus.executablePath": "C:\\tools\\deus\\deus.exe",
  "deus.languageServerPath": "C:\\tools\\deus\\deus-language-server.exe",
  "deus.diagnostics.enable": true
}
```

Settings can be applied globally, per workspace, or per workspace folder.

## Commands

Open the Command Palette with <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>:

| Command | What it does |
| --- | --- |
| **DEUS: Check Current File** | Validates the active `.deus` document and displays process or configuration errors |
| **Format Document** | Formats the active document using `deus fmt` |

## Trust boundary

The extension invokes only `deus fmt` and `deus check`. It does **not** execute DEUS programs, perform retrieval, or grant host capabilities.

The native language server receives the contents of open DEUS documents to provide editor features. CLI-based checking and formatting use temporary copies of the current editor text, including unsaved changes, and remove those copies after use.

## Troubleshooting

### The CLI or language server cannot be found

Configure `deus.executablePath` and `deus.languageServerPath` with absolute paths. Restart VS Code after changing your system `PATH`.

### Diagnostics are too noisy for a workspace

```json
{
  "deus.diagnostics.enable": false
}
```

Then run **DEUS: Check Current File** whenever you want a focused result.

### Formatting is not selected automatically

Choose **Format Document With...**, select **DEUS Language**, and set it as the default formatter for DEUS files.

---

<p align="center">Built for <code>.deus</code> by <strong>reskyume</strong>.</p>
