'use strict';

const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs/promises');
const os = require('os');
const path = require('path');

const languageId = 'deus';
const diagnostics = vscode.languages.createDiagnosticCollection(languageId);
let lspClient;

class DeusLanguageClient {
  constructor() { this.sequence = 0; this.pending = new Map(); this.buffer = Buffer.alloc(0); this.ready = false; }
  start() {
    const configured = vscode.workspace.getConfiguration('deus').get('languageServerPath', 'deus-language-server');
    this.process = cp.spawn(configured.trim() || 'deus-language-server', [], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
    this.process.stdout.on('data', chunk => this.accept(chunk));
    this.process.stderr.on('data', chunk => console.error(`[DEUS LSP] ${chunk.toString('utf8')}`));
    this.process.on('error', error => console.error(`[DEUS LSP] ${error.message}`));
    this.process.on('exit', () => { this.ready = false; for (const item of this.pending.values()) item.reject(new Error('DEUS language server stopped.')); this.pending.clear(); });
    return this.request('initialize', { processId: process.pid, rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null, capabilities: {} })
      .then(() => { this.ready = true; this.notify('initialized', {}); for (const document of vscode.workspace.textDocuments) this.open(document); });
  }
  send(message) {
    const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }), 'utf8');
    this.process.stdin.write(`Content-Length: ${body.length}\r\n\r\n`); this.process.stdin.write(body);
  }
  request(method, params) { const id = ++this.sequence; return new Promise((resolve, reject) => { this.pending.set(id, { resolve, reject }); this.send({ id, method, params }); }); }
  notify(method, params) { if (this.process?.stdin.writable) this.send({ method, params }); }
  accept(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const separator = this.buffer.indexOf('\r\n\r\n'); if (separator < 0) return;
      const match = /Content-Length:\s*(\d+)/i.exec(this.buffer.subarray(0, separator).toString('ascii'));
      if (!match) { this.buffer = this.buffer.subarray(separator + 4); continue; }
      const length = Number(match[1]); if (this.buffer.length < separator + 4 + length) return;
      const body = this.buffer.subarray(separator + 4, separator + 4 + length).toString('utf8');
      this.buffer = this.buffer.subarray(separator + 4 + length);
      try { this.dispatch(JSON.parse(body)); } catch (error) { console.error(`[DEUS LSP] Invalid response: ${error.message}`); }
    }
  }
  dispatch(message) {
    if (message.id !== undefined) {
      const pending = this.pending.get(message.id); if (!pending) return; this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(message.error.message)); else pending.resolve(message.result); return;
    }
    if (message.method === 'textDocument/publishDiagnostics') {
      diagnostics.set(vscode.Uri.parse(message.params.uri), message.params.diagnostics.map(item => {
        const diagnostic = new vscode.Diagnostic(new vscode.Range(item.range.start.line, item.range.start.character, item.range.end.line, item.range.end.character), item.message, vscode.DiagnosticSeverity.Error);
        diagnostic.source = 'deus'; return diagnostic;
      }));
    }
  }
  open(document) { if (this.ready && document.languageId === languageId) this.notify('textDocument/didOpen', { textDocument: { uri: document.uri.toString(), languageId, version: document.version, text: document.getText() } }); }
  change(event) { if (this.ready && event.document.languageId === languageId) this.notify('textDocument/didChange', { textDocument: { uri: event.document.uri.toString(), version: event.document.version }, contentChanges: [{ text: event.document.getText() }] }); }
  close(document) { if (this.ready && document.languageId === languageId) this.notify('textDocument/didClose', { textDocument: { uri: document.uri.toString() } }); }
  async stop() { if (!this.process) return; try { await this.request('shutdown', null); this.notify('exit', null); } catch (_) { this.process.kill(); } }
}

function configuration(document) {
  return vscode.workspace.getConfiguration('deus', document.uri);
}

function workingDirectory(document) {
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  return folder ? folder.uri.fsPath : path.dirname(document.uri.fsPath);
}

function runCli(document, args) {
  const executable = configuration(document).get('executablePath', 'deus').trim() || 'deus';
  return new Promise((resolve, reject) => {
    cp.execFile(executable, args, {
      cwd: workingDirectory(document), encoding: 'utf8', windowsHide: true, maxBuffer: 1024 * 1024
    }, (error, stdout, stderr) => {
      if (error && error.code === 'ENOENT') {
        reject(new Error(`DEUS executable not found: ${executable}. Configure deus.executablePath.`));
        return;
      }
      resolve({ error, stdout, stderr });
    });
  });
}

async function withTemporarySource(document, action) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'deus-vscode-'));
  const sourcePath = path.join(directory, path.basename(document.uri.fsPath || 'document.deus'));
  try {
    await fs.writeFile(sourcePath, document.getText(), 'utf8');
    return await action(sourcePath);
  } finally {
    await fs.rm(directory, { recursive: true, force: true });
  }
}

function parseDiagnostics(output, document) {
  const items = [];
  const pattern = /^(.*):(\d+):(\d+):\s*(error|warning):\s*(.+)$/gm;
  for (const match of output.matchAll(pattern)) {
    const position = new vscode.Position(Math.max(Number(match[2]) - 1, 0), Math.max(Number(match[3]) - 1, 0));
    const validated = document.validatePosition(position);
    const severity = match[4] === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error;
    const diagnostic = new vscode.Diagnostic(new vscode.Range(validated, validated), match[5].trim(), severity);
    diagnostic.source = 'deus';
    items.push(diagnostic);
  }
  return items;
}

async function checkDocument(document, showResult = false) {
  if (document.languageId !== languageId || document.isClosed) return;
  if (!configuration(document).get('diagnostics.enable', true) && !showResult) {
    diagnostics.delete(document.uri);
    return;
  }
  try {
    const result = await withTemporarySource(document, sourcePath => runCli(document, ['check', sourcePath]));
    const output = `${result.stderr}\n${result.stdout}`;
    const parsed = parseDiagnostics(output, document);
    if (result.error && parsed.length === 0) {
      const message = result.stderr.trim() || result.stdout.trim() || `deus check failed (${result.error.message}).`;
      const diagnostic = new vscode.Diagnostic(
        new vscode.Range(document.positionAt(0), document.positionAt(0)),
        message,
        vscode.DiagnosticSeverity.Error
      );
      diagnostic.source = 'deus';
      parsed.push(diagnostic);
      if (showResult) vscode.window.showErrorMessage(message);
    }
    diagnostics.set(document.uri, parsed);
    if (showResult && !result.error) vscode.window.showInformationMessage('DEUS check passed.');
  } catch (error) {
    const diagnostic = new vscode.Diagnostic(
      new vscode.Range(document.positionAt(0), document.positionAt(0)),
      error.message,
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = 'deus';
    diagnostics.set(document.uri, [diagnostic]);
    if (showResult) vscode.window.showErrorMessage(error.message);
  }
}

class DeusFormattingProvider {
  async provideDocumentFormattingEdits(document) {
    try {
      const formatted = await withTemporarySource(document, async sourcePath => {
        const result = await runCli(document, ['fmt', sourcePath]);
        if (result.error) throw new Error(result.stderr.trim() || result.stdout.trim() || 'deus fmt failed.');
        return fs.readFile(sourcePath, 'utf8');
      });
      const source = document.getText();
      const fullRange = new vscode.Range(document.positionAt(0), document.positionAt(source.length));
      return formatted === source ? [] : [vscode.TextEdit.replace(fullRange, formatted)];
    } catch (error) {
      vscode.window.showErrorMessage(error.message);
      return [];
    }
  }
}

function activate(context) {
  lspClient = new DeusLanguageClient();
  lspClient.start().catch(error => console.error(`[DEUS LSP] ${error.message}`));
  context.subscriptions.push(
    diagnostics,
    vscode.languages.registerDocumentFormattingEditProvider(languageId, new DeusFormattingProvider()),
    vscode.commands.registerCommand('deus.checkFile', () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== languageId) {
        vscode.window.showWarningMessage('Open a DEUS file to run the checker.');
        return;
      }
      return checkDocument(editor.document, true);
    }),
    vscode.workspace.onDidOpenTextDocument(document => { lspClient.open(document); if (!lspClient.ready) checkDocument(document); }),
    vscode.workspace.onDidChangeTextDocument(event => lspClient.change(event)),
    vscode.workspace.onDidSaveTextDocument(document => { if (!lspClient.ready) checkDocument(document); }),
    vscode.workspace.onDidCloseTextDocument(document => { lspClient.close(document); diagnostics.delete(document.uri); }),
    vscode.languages.registerHoverProvider(languageId, { provideHover(document, position) {
      if (!lspClient.ready) return null;
      return lspClient.request('textDocument/hover', { textDocument: { uri: document.uri.toString() }, position }).then(result => result ? new vscode.Hover(new vscode.MarkdownString(result.contents.value)) : null);
    }}),
    vscode.languages.registerDefinitionProvider(languageId, { provideDefinition(document, position) {
      if (!lspClient.ready) return null;
      return lspClient.request('textDocument/definition', { textDocument: { uri: document.uri.toString() }, position }).then(result => result ? new vscode.Location(vscode.Uri.parse(result.uri), new vscode.Range(result.range.start.line, result.range.start.character, result.range.end.line, result.range.end.character)) : null);
    }}),
    vscode.languages.registerDocumentSymbolProvider(languageId, { provideDocumentSymbols(document) {
      if (!lspClient.ready) return [];
      return lspClient.request('textDocument/documentSymbol', { textDocument: { uri: document.uri.toString() } }).then(items => items.map(item => new vscode.DocumentSymbol(item.name, '', item.kind, new vscode.Range(item.range.start.line, item.range.start.character, item.range.end.line, item.range.end.character), new vscode.Range(item.selectionRange.start.line, item.selectionRange.start.character, item.selectionRange.end.line, item.selectionRange.end.character))));
    }}),
    vscode.workspace.onDidChangeConfiguration(event => {
      if (event.affectsConfiguration('deus')) {
        for (const document of vscode.workspace.textDocuments) checkDocument(document);
      }
    })
  );
  for (const document of vscode.workspace.textDocuments) if (!lspClient.ready) checkDocument(document);
}

function deactivate() {
  diagnostics.dispose();
  return lspClient?.stop();
}

module.exports = { activate, deactivate };
