'use strict';

const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs/promises');
const os = require('os');
const path = require('path');

const languageId = 'deus';
const diagnostics = vscode.languages.createDiagnosticCollection(languageId);

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
    vscode.workspace.onDidOpenTextDocument(document => checkDocument(document)),
    vscode.workspace.onDidSaveTextDocument(document => checkDocument(document)),
    vscode.workspace.onDidCloseTextDocument(document => diagnostics.delete(document.uri)),
    vscode.workspace.onDidChangeConfiguration(event => {
      if (event.affectsConfiguration('deus')) {
        for (const document of vscode.workspace.textDocuments) checkDocument(document);
      }
    })
  );
  for (const document of vscode.workspace.textDocuments) checkDocument(document);
}

function deactivate() {
  diagnostics.dispose();
}

module.exports = { activate, deactivate };
