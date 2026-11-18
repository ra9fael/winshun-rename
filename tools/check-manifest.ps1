#!/usr/bin/env pwsh
# check-manifest.ps1 — the result-menu switch has to be wired end to end.
#
# Why this is a gate and not a note. `triggers.files.if` names a bool setting
# the host reads before it builds the file menu: the entry shows only while
# that setting is on, and the setting's default decides what a fresh install
# sees. Three mistakes load fine in any host and surface only as "the switch
# does nothing": a missing `if` (nothing for the host to switch on), a default
# of false (the entry starts hidden), and a floor below 0.33.0 (older hosts
# ignore `if` and always show the entry). The host rejects a key that is not a
# declared bool setting (WinShun src/plugin/Manifest.cpp), but it cannot know
# a warning was meant to be a switch.
#
# Usage: pwsh -File tools/check-manifest.ps1 [-Manifest <path>]
# Exit:  0 = clean, 1 = violation

param([string]$Manifest = '')

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Manifest)) {
    $Manifest = Join-Path $repoRoot 'package/plugin.json'
}

if (-not (Test-Path -LiteralPath $Manifest)) {
    Write-Error "plugin.json not found at $Manifest"
    exit 1
}

$doc = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$problems = [System.Collections.Generic.List[string]]::new()

# The first host that honours `triggers.files.if` (WinShun docs/plugins.md,
# section "settings").
$ifFloor = [version]'0.33.0'

$fileTriggers = @()
foreach ($command in @($doc.commands)) {
    foreach ($trigger in @($command.triggers)) {
        if ($null -ne $trigger.files) {
            $fileTriggers += , @($command, $trigger.files)
        }
    }
}

if ($fileTriggers.Count -eq 0) {
    $problems.Add('no triggers[].files: the result-menu entry is gone')
}

foreach ($pair in $fileTriggers) {
    $command = $pair[0]
    $files = $pair[1]

    if (-not ($files.PSObject.Properties.Name -contains 'if')) {
        $problems.Add("$($command.id): files trigger has no `"if`", so the menu cannot be hidden")
        continue
    }

    $key = [string]$files.if
    $setting = @($doc.settings) | Where-Object { $_.key -eq $key } | Select-Object -First 1
    if ($null -eq $setting) {
        $problems.Add("$($command.id): files.if names `"$key`", which is not a declared setting")
    }
    elseif ([string]$setting.type -ne 'bool') {
        $problems.Add("$($command.id): files.if names `"$key`", a $($setting.type) setting; the host only accepts bool")
    }
    elseif ($setting.default -ne $true) {
        $problems.Add("$($command.id): `"$key`" defaults to $($setting.default); the entry would start hidden")
    }

    if ($null -ne $files.max -and ([int]$files.max -lt 1 -or [int]$files.max -gt 1000)) {
        $problems.Add("$($command.id): files.max is $($files.max); the host accepts 1 to 1000")
    }
}

if ($null -ne $doc.minWinShun) {
    $floor = [version]([string]$doc.minWinShun)
    if ($fileTriggers.Count -gt 0 -and $floor -lt $ifFloor) {
        $problems.Add("minWinShun is $floor; a host before $ifFloor ignores files.if, so the switch would not take effect")
    }
}
else {
    $problems.Add('minWinShun is missing; the host version that honours files.if has to be stated')
}

if ($problems.Count -eq 0) {
    Write-Host 'manifest: clean'
    exit 0
}

Write-Host 'manifest: VIOLATION'
foreach ($problem in $problems) { Write-Host "  - $problem" }
Write-Host ''
Write-Host "A bool setting with `"default`": true, named by triggers.files.if, and"
Write-Host "minWinShun >= $ifFloor is what makes the menu switch real."
exit 1

