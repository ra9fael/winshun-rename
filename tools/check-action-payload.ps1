#!/usr/bin/env pwsh
# check-action-payload.ps1 — the plugin must read an action's payload where the
# host puts it.
#
# Why this is a gate and not a note. A row's own fields live in `data`, and the
# host hands the item back as it came (WinShun Items.cpp::itemToJson). So the
# JSON `action()` receives carries `data.openFolder` / `data.openWindow`, never
# `openFolder` / `openWindow` at the top. Reading only the top level does not fail
# loudly: it answers "No files selected" to a row that plainly carried a folder,
# which looks like a UI problem and is not.
#
# This is a source check rather than a runtime one because the contract is
# "where the code looks", which a grep states exactly and a unit test can only
# sample. LS-S10-01…04 in TEST-SPEC.md are the behavioural halves.
# Usage: pwsh -File tools/check-action-payload.ps1
# Exit:  0 = clean, 1 = violation

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$main = Join-Path $repoRoot 'main.cpp'

if (-not (Test-Path -LiteralPath $main))
{
    Write-Error "main.cpp not found at $main"
    exit 1
}

$text = Get-Content -LiteralPath $main -Raw
$problems = [System.Collections.Generic.List[string]]::new()

# 1. action() must go through the unwrapped payload, once, near the top.
if ($text -notmatch 'const\s+winshun::Json\s+payload\s*=\s*a\.item\["data"\]')
{
    $problems.Add('action() does not unwrap a.item["data"] into `payload`')
}

# 2. Nothing in action() may read a payload field off a.item directly. Matched
#    only for the keys the plugin itself puts in `data`, so unrelated top-level
#    reads (id, title) stay legal.
foreach ($key in @('openFolder', 'openWindow'))
{
    if ($text -match ('a\.item\["' + $key + '"\]'))
    {
        $problems.Add("action() reads a.item[""$key""] — it lives in data, so it is payload[""$key""]")
    }
}

# 3. The keys have to be published in `data`: an action can only receive what
#    the row carried. One `withData` object per row must mention each key used.
foreach ($key in @('openFolder', 'openWindow'))
{
    if ($text -notmatch ('"' + $key + '"\s*,'))
    {
        $problems.Add("no row publishes `$key` in its data")
    }
}

if ($problems.Count -eq 0)
{
    Write-Host 'action payload: clean'
    exit 0
}

Write-Host 'action payload: VIOLATION'
foreach ($p in $problems) { Write-Host "  - $p" }
Write-Host ''
Write-Host 'The host hands the item back as it came, so a row''s fields come back'
Write-Host 'under `data`. Read them from payload = a.item["data"], and keep the'
Write-Host 'top-level fallback so the once-plugin shape still works.'
exit 1
