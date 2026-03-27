param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Args
)

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$npmCli = Join-Path $workspaceRoot "tools\npm\bin\npm-cli.js"
$cacheDir = Join-Path $workspaceRoot ".npm-cache"

if (-not (Test-Path $npmCli)) {
  Write-Error "Local npm CLI not found at $npmCli"
  exit 1
}

$env:NPM_CONFIG_OFFLINE = "false"
$env:npm_config_cache = $cacheDir
$env:NPM_CONFIG_CACHE = $cacheDir

& node $npmCli @Args
exit $LASTEXITCODE
