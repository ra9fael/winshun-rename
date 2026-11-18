# negatives.ps1 — the manifest gate has to fail, not just pass.
#
# Runs tools/check-manifest.ps1 against five temporary copies of the manifest,
# each broken in one way the gate is meant to catch, and prints each exit code.
# Exit: 0 when every copy was refused.
#
# Usage: pwsh -File docs/validation/rename-menu-20261119/negatives.ps1

param([string]$Check = '',
      [string]$Source = '')

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrEmpty($Check)) { $Check = Join-Path $repoRoot 'tools/check-manifest.ps1' }
if ([string]::IsNullOrEmpty($Source)) { $Source = Join-Path $repoRoot 'package/plugin.json' }

$work = Join-Path $env:TEMP 'opencode\manifest-negatives'
New-Item -ItemType Directory -Path $work -Force | Out-Null

$failed = 0
$variants = 0
function NewVariant([string]$name, [scriptblock]$mutate) {
    $doc = Get-Content -LiteralPath $Source -Raw | ConvertFrom-Json
    & $mutate $doc
    $path = Join-Path $work "$name.json"
    $doc | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path -Encoding utf8NoBOM
    Write-Host "---- $name"
    & $Check -Manifest $path
    Write-Host "exit=$LASTEXITCODE"
    $script:variants++
    if ($LASTEXITCODE -eq 0) { $script:failed++ }
}

NewVariant 'no-if' { param($doc) $doc.commands[0].triggers[0].files.PSObject.Properties.Remove('if') }
NewVariant 'default-false' { param($doc) $doc.settings[0].default = $false }
NewVariant 'not-bool' { param($doc) $doc.settings[0].type = 'text' }
NewVariant 'floor-0.32.0' { param($doc) $doc.minWinShun = '0.32.0' }
NewVariant 'wrong-key' { param($doc) $doc.commands[0].triggers[0].files.if = 'showInMenu' }

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
if ($failed -eq 0) { Write-Host "all $($script:variants) variants refused"; exit 0 }
Write-Host "$failed variant(s) passed the gate"; exit 1
