# Optional Windows check of a built release (not part of `make test`; game-free, no game folder is touched):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_setup_windows.ps1 -Release <local\release folder> -Version <v>
# From WSL: powershell.exe ... -File "$(wslpath -w tools/test_setup_windows.ps1)" -Release "$(wslpath -w local/release)" -Version v...
# Copies the release to %TEMP% first (programs started from \\wsl.localhost can stop at Windows' open-file prompt), then:
# the single-file Setup's /extract, the launcher's --setup verbs on fake folders with a scratch --data-dir, and the same
# verbs through both Setup forms. The single-file Setup unpacks into %LOCALAPPDATA%\DeathStrandingCoop\setup\<version>;
# that folder is removed at the end. Exit 1 when an answer is not the expected one.
param([Parameter(Mandatory)][string]$Release, [Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Join-Path $env:TEMP ('ds-setup-check-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $root | Out-Null
$failed = 0
function Run($file, [string[]]$arguments) {   # at most 30 s; never leaves the process behind
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $file; $info.Arguments = ($arguments -join ' '); $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($info)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(30000)) { $p.Kill(); return [pscustomobject]@{ Code = -1; Out = ''; Err = 'TIMEOUT' } }
    return [pscustomobject]@{ Code = $p.ExitCode; Out = "$($o.Result)"; Err = "$($e.Result)" }
}
function Expect($what, $result, [int]$code, [string]$text) {
    $ok = $result.Code -eq $code -and ("$($result.Out)$($result.Err)").Contains($text)
    if (-not $ok) { $script:failed = 1 }
    Write-Output ('{0} {1}: exit {2}' -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $what, $result.Code)
}
$working = Join-Path $env:LOCALAPPDATA "DeathStrandingCoop\setup\$Version"
$hadWorking = Test-Path -LiteralPath $working
try {
    Copy-Item -LiteralPath (Join-Path $Release 'DeathStrandingCoop-Setup.exe') -Destination (Join-Path $root 'single.exe')
    Copy-Item -LiteralPath (Join-Path $Release "deathstranding-coop-$Version") -Destination (Join-Path $root 'pkg') -Recurse
    $x = Join-Path $root 'x'
    $r = Run (Join-Path $root 'single.exe') @('/extract', "`"$x`"")
    Expect '/extract' $r 0 ''
    if (@(Get-ChildItem -Recurse -File $x).Count -ne 11) { $failed = 1; Write-Output 'FAIL /extract wrote another number of files' }
    $empty = Join-Path $root 'empty'; New-Item -ItemType Directory $empty | Out-Null
    $other = Join-Path $root 'other'; New-Item -ItemType Directory $other | Out-Null; Set-Content (Join-Path $other 'ds.exe') 'not the game'
    $data = Join-Path $root 'data'
    $launcher = Join-Path $x 'deathstranding-coop.exe'
    Expect 'launcher status, no ds.exe' (Run $launcher @('--setup', 'status', '--json', "`"$empty`"", '--data-dir', "`"$data`"")) 1 '"state":"not-a-game-folder"'
    Expect 'launcher status, another build' (Run $launcher @('--setup', 'status', "`"$other`"", '--data-dir', "`"$data`"")) 3 'unsupported-build'
    Expect 'launcher install, another build' (Run $launcher @('--setup', 'install', "`"$other`"", '--yes', '--data-dir', "`"$data`"")) 3 'not the one the co-op mod supports'
    Expect 'launcher install without --yes' (Run $launcher @('--setup', 'install', "`"$other`"", '--data-dir', "`"$data`"")) 2 'need --yes'
    if (Test-Path $data) { $failed = 1; Write-Output 'FAIL a refused install wrote the data folder' }
    $zipSetup = Join-Path $root 'pkg\DeathStrandingCoop-Setup.exe'
    Expect 'zip Setup status' (Run $zipSetup @('status', "`"$other`"")) 3 'unsupported-build'
    Expect 'zip Setup status --json' (Run $zipSetup @('status', '--json', "`"$empty`"")) 1 '"state":"not-a-game-folder"'
    Expect 'zip Setup unknown verb' (Run $zipSetup @('frobnicate')) 2 ''
    Expect 'single Setup status' (Run (Join-Path $root 'single.exe') @('status', "`"$other`"")) 3 'unsupported-build'
} finally {
    Remove-Item -Recurse -Force $root
    if (-not $hadWorking -and (Test-Path -LiteralPath $working)) { Remove-Item -LiteralPath $working -Recurse -Force }
    $setup = Split-Path -Parent $working
    if ((Test-Path -LiteralPath $setup) -and -not (Get-ChildItem -LiteralPath $setup -Force)) { Remove-Item -LiteralPath $setup }
}
exit $failed
