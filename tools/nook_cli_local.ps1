$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
$env:PYTHONPATH = Join-Path $repoRoot "host\nook-py"

$python = if ($env:NOOK_LOCAL_PYTHON) { $env:NOOK_LOCAL_PYTHON } else { "python" }

& $python -m nook.cli @args
exit $LASTEXITCODE
