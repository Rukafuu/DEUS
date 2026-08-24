# DEUS Language for VS Code

Official language support for `.deus` retrieval and crawling programs, published under `reskyume`.

## Features

- syntax highlighting, comments, brackets and indentation;
- formatting through `deus fmt` (`Format Document`);
- diagnostics through `deus check` when a file is opened or saved;
- live native-parser diagnostics while editing;
- keyword hover, document outline and same-file definition lookup;
- **DEUS: Check Current File** command;
- snippets for programs, bindings, output, records and HTTP retrieval.

Formatting and checking use a temporary copy containing the current editor text. Unsaved changes are supported and the extension does not rewrite the source file behind VS Code's back.

## Requirements and configuration

Install the DEUS CLI separately and ensure `deus` is on `PATH`, or configure its path:

```json
{
  "deus.executablePath": "C:\\tools\\deus\\deus.exe",
  "deus.languageServerPath": "C:\\tools\\deus\\deus-language-server.exe",
  "deus.diagnostics.enable": true,
  "[deus]": {
    "editor.defaultFormatter": "reskyume.deus-language",
    "editor.formatOnSave": true
  }
}
```

Both executable paths are passed directly to the operating system without a shell. Diagnostics can be disabled per workspace or folder with `deus.diagnostics.enable`.

## Troubleshooting

If the CLI cannot be found, set `deus.executablePath` to an absolute executable path. Run **DEUS: Check Current File** to surface configuration or process errors explicitly; automatic checks otherwise remain unobtrusive.

The extension invokes only `deus fmt` and `deus check`. It does not execute DEUS programs or grant host capabilities.
