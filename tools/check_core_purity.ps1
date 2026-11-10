#!/usr/bin/env pwsh
# check_core_purity.ps1 — enforce the architectural constraint from
# DEVELOPMENT-PLAN.md §4: nothing under core/ may pull in Windows or Qt headers.
#
# This is a real build gate, not a style preference. The whole point of the pure
# core is that the L1 tests build and run on any platform, and that the control
# mode and oil mode share one finalization pipeline. The moment a <windows.h>
# include appears here, both properties are gone and the correctness argument has
# to be made twice.
#
# Only #include lines are checked. A comment that mentions windows.h (this file's
# sibling header documents the rule, so one does) is not a violation.
#
# Usage: pwsh -File tools/check_core_purity.ps1
# Exit:  0 = clean, 1 = violation

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$coreDir = Join-Path $repoRoot 'core'

if (-not (Test-Path -LiteralPath $coreDir))
{
    Write-Error "core/ not found at $coreDir"
    exit 1
}

# Matched case-insensitively against the include target only.
$forbidden = @(
    'windows.h', 'windowsx.h', 'winbase.h', 'windef.h', 'shellapi.h',
    'shlobj.h', 'shobjidl.h', 'commctrl.h', 'winternl.h',
    'QtCore', 'QtGui', 'QtQml', 'QtQuick', 'QtWidgets', 'QString', 'QObject'
)

$violations = @()

Get-ChildItem -LiteralPath $coreDir -File -Recurse |
    Where-Object { $_.Extension -in '.cpp', '.h', '.hpp' } |
    ForEach-Object {
        $file = $_
        $lineNumber = 0
        foreach ($line in (Get-Content -LiteralPath $file.FullName))
        {
            $lineNumber++
            # An include directive is the only thing that can actually pull a header in.
            if ($line -notmatch '^\s*#\s*(include|import)\b')
            {
                continue
            }
            foreach ($name in $forbidden)
            {
                if ($line -match [regex]::Escape($name))
                {
                    $violations += "{0}:{1}: {2}" -f $file.Name, $lineNumber, $line.Trim()
                    break
                }
            }
        }
    }

if ($violations.Count -gt 0)
{
    Write-Host 'FAIL: core/ must stay free of Windows and Qt headers.' -ForegroundColor Red
    $violations | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'Move the platform-dependent code into win/ or ui/ instead.' -ForegroundColor Yellow
    exit 1
}

Write-Host 'OK: core/ has no Windows or Qt includes.' -ForegroundColor Green
exit 0
