param([Parameter(Mandatory = $true)][string]$Server)

$messages = @(
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}',
    '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///smoke.deus","languageId":"deus","version":1,"text":"flow main:\n    bind answer = 42\n    load answer\n    emit\n"}}}',
    '{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///smoke.deus"},"position":{"line":1,"character":5}}}',
    '{"jsonrpc":"2.0","id":3,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///smoke.deus"}}}',
    '{"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///smoke.deus"},"position":{"line":2,"character":9}}}',
    '{"jsonrpc":"2.0","id":5,"method":"shutdown","params":null}',
    '{"jsonrpc":"2.0","method":"exit","params":null}'
)

$wire = ''
foreach ($message in $messages) {
    $length = [Text.Encoding]::UTF8.GetByteCount($message)
    $wire += "Content-Length: $length`r`n`r`n$message"
}

$start = [Diagnostics.ProcessStartInfo]::new($Server)
$start.UseShellExecute = $false
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($start)
$process.StandardInput.Write($wire)
$process.StandardInput.Close()
$output = $process.StandardOutput.ReadToEnd()
$errors = $process.StandardError.ReadToEnd()
$process.WaitForExit()

if ($process.ExitCode -ne 0) { throw "language server exited $($process.ExitCode): $errors" }
if ($output.Contains("`r`r`n")) { throw 'language server emitted invalid CRCRLF framing' }
foreach ($expected in @('publishDiagnostics', '`bind`', '"name":"answer"', '"character":9')) {
    if (!$output.Contains($expected)) { throw "language server response missing: $expected" }
}

Write-Output 'LSP protocol smoke passed'
