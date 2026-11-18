#!/usr/bin/env pwsh
# host-sim.ps1 — drive the plugin the way Win顺 does, without Win顺.
#
# Why this exists: the plugin's contract is JSON-RPC over stdio with the host's
# runner environment (PATH prefixed by Win顺's folder, WS_QT_DIR, the Qt plugin and
# QML paths — see WinShun/src/plugin/runner/Sandbox.cpp withHostQt). Driving that
# from a script is the closest thing to an integration test that does not need the
# host running, and it is what catches the failures a unit test cannot: a missing
# DLL, a QML error at load, a timeout in initialize.
#
# Usage:
#   pwsh -File tools\host-sim.ps1 -Exe .\rename.exe -Folder D:\some\dir
#   pwsh -File tools\host-sim.ps1 -Exe .\rename.exe -Files a.txt b.txt
#   pwsh -File tools\host-sim.ps1 -Exe .\rename.exe -QueryText "report -> 报告"
#
# Exit code: 0 when every exchange answered as expected, 1 otherwise.

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$HostDir = "C:\Program Files\WinShun",
    [string]$Folder,
    [string[]]$Files,
    [string]$QueryText,     # e.g. "report -> 报告"; the keyword box's own text
    [string[]]$Hits,         # the paths Win顺's index answers the query with
    [string]$Rule,           # "<find>`n<replace>" for the window grab hook
    [switch]$Apply,
    [string]$Grab,
    [int]$InitTimeoutMs = 3000
)

$ErrorActionPreference = "Stop"
# The protocol is UTF-8 and the rows carry Chinese: write the console in the
# same encoding so a captured run reads back as written.
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

if (-not (Test-Path $Exe)) { throw "exe not found: $Exe" }
if (-not (Test-Path (Join-Path $HostDir "Qt6Core.dll"))) { throw "host Qt not found under $HostDir" }

$failures = 0
# @($null) is an array of one in PowerShell: a missing -Hits has to be zero.
$hitList = @($Hits | Where-Object { $_ })
function Check([bool]$ok, [string]$label, [string]$detail = "") {
    if ($ok) { Write-Host "[  OK  ] $label" }
    else { Write-Host "[ FAIL ] $label $detail"; $script:failures++ }
}

# A bare ReadLine orbits forever when the plugin never answers, and the whole
# test then hangs with no diagnosis. One pending read per stream (awaited, not
# racing a second one), and a kill on timeout: the plugin is only ever asked one
# question at a time, so the read is safe to abandon by ending the process.
function Read-Line([int]$TimeoutMs = 15000) {
    $task = $proc.StandardOutput.ReadLineAsync()
    try {
        if ($task.Wait($TimeoutMs)) { return $task.Result }
    } catch {
        Write-Host "[ FAIL ] read threw: $($_.Exception.Message)"
        $script:failures++
        return $null
    }
    Write-Host "[ FAIL ] no answer within ${TimeoutMs}ms; killing pid=$($proc.Id)"
    $script:failures++
    try { $proc.Kill() } catch {}
    return $null
}

# ---- the host's runner environment, copied from Sandbox.cpp::withHostQt ------
$env:PATH = "$HostDir;$env:PATH"
$env:WS_QT_DIR = $HostDir
$env:WS_QT_QML_DIR = "$HostDir\qml"
$env:QT_PLUGIN_PATH = $HostDir
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$HostDir\platforms"
$env:QML_IMPORT_PATH = "$HostDir\qml"
$env:QML2_IMPORT_PATH = "$HostDir\qml"
$env:QT_QUICK_CONTROLS_STYLE = "Basic"
if ($Grab) { $env:RENAME_GRAB_WINDOW = $Grab } else { Remove-Item Env:RENAME_GRAB_WINDOW -ErrorAction SilentlyContinue }
if ($Rule) { $env:RENAME_GRAB_RULE = $Rule } else { Remove-Item Env:RENAME_GRAB_RULE -ErrorAction SilentlyContinue }
if ($Apply) { $env:RENAME_GRAB_APPLY = "1" } else { Remove-Item Env:RENAME_GRAB_APPLY -ErrorAction SilentlyContinue }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = (Resolve-Path $Exe).Path
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
# The protocol is UTF-8 (docs/plugins.md); .NET would otherwise decode stdout
# with the console code page and turn a Chinese subtitle into a parse error.
$psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
$psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
$psi.WorkingDirectory = (Split-Path -Parent (Resolve-Path $Exe).Path)

$proc = [System.Diagnostics.Process]::Start($psi)
Write-Host "started pid=$($proc.Id) exe=$($psi.FileName)"

# ---- initialize, timed: the host gives timeoutMs and kills what is late -------
$dataDir = Join-Path $env:TEMP ("renamesim-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $dataDir -Force | Out-Null

$init = @{
    jsonrpc = "2.0"; id = 1; method = "initialize"
    params = @{
        protocolVersion = "1"
        winshun = "0.22.0"
        capabilities = @{ hostHotkeys = $false; details = $true }
        pluginDir = $psi.WorkingDirectory
        dataDir = $dataDir
        locale = "zh"
        theme = "dark"
        appearance = @{
            locale = "zh"; theme = "dark"; themeSetting = "system"; corners = "round"
            uiStyle = "fluent"; uiSize = "standard"; transparency = "on"; renderer = "d3d11"
            nativeLook = @{
                style = "fluent"; dark = $true; rounded = $true
                menuRadius = 8; cardRadius = 6; controlRadius = 4
                largeControlRadius = 6; smallControlRadius = 3
                background = "#ff202020"; layer = "#ff272727"; page = "#ff202020"
                windowEdge = "#ff6a6a6a"; divider = "#ff2e2e2e"
                text = "#fff3f3f3"; subtext = "#ffb8b8b8"; faint = "#ff8a8a8a"
                hover = "#ff292929"; pressed = "#ff323232"; tagFill = "#1effffff"
                control = "#ff373737"; controlHover = "#ff3c3c3c"; controlBorder = "#ff444444"
                inputFocused = "#ff1f1f1f"
                accent = "#ff4cc2ff"; accentFill = "#ff0067c0"; accentText = "#ffffffff"
                danger = "#ffff99a4"; dangerFill = "#ffc42b1c"
            }
        }
        settings = @{}
    }
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$proc.StandardInput.WriteLine(($init | ConvertTo-Json -Depth 12 -Compress))
$proc.StandardInput.Flush()
$first = Read-Line
$sw.Stop()

Check ($null -ne $first) "initialize answered" "(no line)"
Check ($sw.ElapsedMilliseconds -lt $InitTimeoutMs) "initialize within ${InitTimeoutMs}ms" "($($sw.ElapsedMilliseconds)ms)"
if ($first) { Check ($first -match 'ra9fael\.rename') "initialize names the plugin" "($first)" }
Write-Host "initialize took $($sw.ElapsedMilliseconds) ms"

# ---- the query the host sends for the keyword box ---------------------------
$queryParams = @{
    queryId = 7; command = "rename"; trigger = "keyword"; keyword = "rename"
    text = ""; raw = "rename"; limit = 200; filters = @{}
}
if ($Folder) { $queryParams.filters.folder = $Folder }
if ($QueryText) { $queryParams.text = $QueryText; $queryParams.raw = "rename $QueryText" }

$queryMsg = @{ jsonrpc = "2.0"; id = 2; method = "query"; params = $queryParams }
$proc.StandardInput.WriteLine(($queryMsg | ConvertTo-Json -Depth 12 -Compress))
$proc.StandardInput.Flush()

# A query may call winshun/tool first (search_files), so answer it like the host:
# read lines until the query's own id comes back. Synchronous reads: one pending
# read per stream, which is what keeps the pipe from throwing.
$rows = $null
while ($true) {
    $line = Read-Line
    if ($null -eq $line) { break }
    Write-Host "  <- $($line.Substring(0, [Math]::Min(160, $line.Length)))"
    if ($line -match '"method"\s*:\s*"winshun/tool"') {
        # Pretend to be the host: the reply is the tool's structured result, flat
        # (WinShun's Host::onToolCall answers with `result.structured`).
        $request = $line | ConvertFrom-Json
        $id = $request.id
        Write-Host ("        (asked for " + $request.params.name + ")")
        if ($Folder -and $QueryText) {
            # The launcher was inside a folder: the search has to stay in it.
            $scoped = @($request.params.arguments.folders) -contains $Folder
            Check $scoped "search kept the folder scope" "($line)"
        }
        $items = @()
        foreach ($p in $hitList) {
            if (-not $p) { continue }
            $items += @{ path = [System.IO.Path]::GetFullPath($p); name = (Split-Path -Leaf $p); type = "file" }
        }
        $reply = @{ jsonrpc = "2.0"; id = $id; result = @{ items = $items; returned = $items.Count; truncated = $false; elapsed_ms = 1 } }
        $proc.StandardInput.WriteLine(($reply | ConvertTo-Json -Depth 12 -Compress))
        $proc.StandardInput.Flush()
        continue
    }
    if ($line -match '"id"\s*:\s*2\b') {
        $rows = $line | ConvertFrom-Json
        break
    }
}
Check ($null -ne $rows) "query answered"
if ($rows) {
    $count = @($rows.result.items).Count
    Write-Host "query returned $count item(s)"
    $rows.result.items | ForEach-Object {
        Write-Host ("   - [" + $_.id + "] " + $_.title + "  |  " + $_.subtitle)
    }
}

# ---- act on the folder row, the way a click does ----------------------------
if ($rows) {
    $openRow = $rows.result.items | Where-Object { $_.data -and $_.data.openFolder } | Select-Object -First 1
    if ($openRow) {
        $act = @{
            jsonrpc = "2.0"; id = 3; method = "action"
            params = @{ command = "rename"; action = "default"; item = $openRow; text = "" }
        }
        $proc.StandardInput.WriteLine(($act | ConvertTo-Json -Depth 12 -Compress))
        $proc.StandardInput.Flush()
        $acted = Read-Line
        Check ($null -ne $acted -and $acted -match '"id":3') "folder row answered" "($acted)"
        # The row must have been understood: "hide" (a window opened) or, in a
        # build without Qt, a notice naming the folder. Anything else — above all
        # "No files selected" — means the payload was not read.
        Check ($null -ne $acted -and ($acted -match '"hide"' -or $acted -match 'no window|没有窗口')) `
            "folder row acted on its payload" "($acted)"
    }

    # The keyword search: the batch row carries every path Win顺's index answered
    # with; acting on it opens the window on exactly those.
    $allRow = $rows.result.items | Where-Object { $_.data -and $_.data.openPaths } | Select-Object -First 1
    if ($QueryText) {
        if ($hitList.Count -eq 0) {
            $noneRow = $rows.result.items | Where-Object { $_.id -eq "batch.none" } | Select-Object -First 1
            Check ($null -ne $noneRow) "search said it found nothing" "(no batch.none row)"
        }
        else {
            Check ($null -ne $allRow) "search returned a batch row" "(none carried openPaths)"
            $oneRows = @($rows.result.items | Where-Object { $_.data -and $_.data.openPath })
            Check ($oneRows.Count -ge 1) "search returned per-file rows" "($($oneRows.Count))"
            if ($allRow) {
                Check (@($allRow.data.openPaths).Count -eq $hitList.Count) `
                    "batch row carries every match" "($(@($allRow.data.openPaths).Count) vs $($hitList.Count))"
                $act = @{
                    jsonrpc = "2.0"; id = 6; method = "action"
                    params = @{ command = "rename"; action = "default"; item = $allRow; text = "" }
                }
                $proc.StandardInput.WriteLine(($act | ConvertTo-Json -Depth 12 -Compress))
                $proc.StandardInput.Flush()
                $acted = Read-Line
                Check ($null -ne $acted -and $acted -match '"id":6') "batch row answered" "($acted)"
                Check ($null -ne $acted -and ($acted -match '"hide"' -or $acted -match 'no window|没有窗口')) `
                    "batch row opened the window on the matches" "($acted)"
            }
        }
    }
}

# ---- the result menu's selection: one action carrying the paths -------------
# The host sends trigger "files", action "files", and the chosen full paths; the
# plugin must take them as its input set instead of scanning a parent folder.
if ($Files) {
    $chosen = @($Files | ForEach-Object {
        if ([System.IO.Path]::IsPathRooted($_)) { [System.IO.Path]::GetFullPath($_) }
        else { [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $_)) }
    })
    $filesAct = @{
        jsonrpc = "2.0"; id = 5; method = "action"
        params = @{ command = "rename"; action = "files"; trigger = "files"; files = $chosen; text = "" }
    }
    $proc.StandardInput.WriteLine(($filesAct | ConvertTo-Json -Depth 12 -Compress))
    $proc.StandardInput.Flush()
    $acted = Read-Line
    Check ($null -ne $acted -and $acted -match '"id":5') "files action answered" "($acted)"
    Check ($null -ne $acted -and ($acted -match '"hide"' -or $acted -match 'no window|没有窗口')) `
        "files action opened the window" "($acted)"
}

# ---- shutdown, as the host does ---------------------------------------------
$proc.StandardInput.WriteLine('{"jsonrpc":"2.0","id":9,"method":"shutdown"}')
$proc.StandardInput.Flush()
$bye = Read-Line
Check ($null -ne $bye -and $bye -match '"id":9') "shutdown answered" "($bye)"

$proc.StandardInput.Close()
if (-not $proc.WaitForExit(8000)) { $proc.Kill() }
$stderr = $proc.StandardError.ReadToEnd()
if ($stderr) {
    Write-Host "--- stderr ---"
    Write-Host $stderr
}
if ($Files) {
    # The plugin writes the selection it received to stderr; that line is the
    # record that every path arrived, not just a count.
    Check ($stderr -match ('window: count=' + $chosen.Count)) "files action received the selection" "($stderr)"
    foreach ($path in $chosen) {
        Check ($stderr.Contains($path)) "files action received $path" "($stderr)"
    }
}
if ($QueryText -and $Hits) {
    Check ($stderr -match ('window: count=' + $hitList.Count)) "search batch received the matches" "($stderr)"
    foreach ($path in $hitList) {
        Check ($stderr.Contains([System.IO.Path]::GetFullPath($path))) "search batch received $path" "($stderr)"
    }
}

Remove-Item $dataDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
if ($failures -eq 0) { Write-Host "all checks passed"; exit 0 }
Write-Host "$failures check(s) failed"; exit 1

