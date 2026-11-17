# Death Stranding Co-op Setup: what only Windows can do (desktop shortcut, Windows Firewall, invite links) and the calls
# into the launcher, which makes every file decision (deathstranding-coop.exe --setup ...; launcher/install.cpp holds the
# one install logic, shared with the launcher's own window). Windows PowerShell 5.1, no other tools, no network.
# Dot-sourced by installer-gui.ps1 (the window) and deathstranding-coop-setup.ps1 (the verbs).
$script:DsPackage   = $PSScriptRoot
$script:DsLauncher  = Join-Path $PSScriptRoot 'deathstranding-coop.exe'      # this package's launcher: runs the --setup verbs
$script:DsData      = Join-Path $env:LOCALAPPDATA 'DeathStrandingCoop'
$script:DsApp       = Join-Path $script:DsData 'app'                         # where install puts the launcher and its package
$script:DsInstalled = Join-Path $script:DsApp 'deathstranding-coop.exe'
$script:DsShortcut  = 'Death Stranding Co-op.lnk'
$script:DsProtocol  = 'HKCU:\Software\Classes\deathstranding-coop'           # the launcher's own --register-protocol key
$script:DsExitFailed = 1; $script:DsExitUsage = 2; $script:DsExitUnsupported = 3; $script:DsExitRunning = 4

function Get-DsVersion {
    try { return (Get-Content -LiteralPath (Join-Path $script:DsPackage 'coop-mod.json') -Raw | ConvertFrom-Json).version } catch { return '' }
}

# One argument for a Windows command line: quoted, inner quotes escaped, trailing backslashes doubled.
function ConvertTo-DsArgument([string]$value) {
    if ($value -match '^[A-Za-z0-9._:/=-]+$') { return $value }
    $escaped = $value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}
# Start a program without a window, wait for it and capture both streams.
function Invoke-DsHidden([string]$file, [string[]]$arguments) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $file
    $info.Arguments = ($arguments | ForEach-Object { ConvertTo-DsArgument $_ }) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $output.Result; Error = $errors.Result }
    } finally { $process.Dispose() }
}
# The launcher's Setup verbs: status [--json] [<game>], install <game> --yes, uninstall <game> --yes.
function Invoke-DsSetup([string[]]$arguments) {
    if (-not (Test-Path -LiteralPath $script:DsLauncher -PathType Leaf)) {
        return [pscustomobject]@{ ExitCode = $script:DsExitFailed; Output = ''; Error = 'deathstranding-coop.exe is missing beside Setup. Extract the whole package again.' }
    }
    return Invoke-DsHidden $script:DsLauncher (@('--setup') + $arguments)
}
# Every install the launcher finds in the Steam libraries, or one folder: the launcher's JSON reports (install.cpp reportJson).
function Get-DsCards([string]$extra = '') {
    $cards = @()
    $all = Invoke-DsSetup @('status', '--json')
    if ($all.ExitCode -eq 0 -and $all.Output) { try { $cards += @(($all.Output | ConvertFrom-Json).games) } catch { } }
    if ($extra -and -not ($cards | Where-Object { $_.game -eq $extra })) {
        $one = Invoke-DsSetup @('status', '--json', $extra)
        try { $cards += ($one.Output | ConvertFrom-Json) } catch {
            $cards += [pscustomobject]@{ game = $extra; state = 'not-a-game-folder'; button = 'Needs attention'; problems = @("$($one.Error)".Trim()) }
        }
    }
    return $cards
}

# ---- the three options (co-op mod standard G3); uninstall removes all three ----
function Get-DsShortcutTarget {
    $file = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) $script:DsShortcut
    if (-not (Test-Path -LiteralPath $file)) { return '' }
    $shell = New-Object -ComObject WScript.Shell
    try { return $shell.CreateShortcut($file).TargetPath } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
}
function Set-DsShortcut {
    $shell = New-Object -ComObject WScript.Shell
    try {
        $link = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) $script:DsShortcut))
        $link.TargetPath = $script:DsInstalled
        $link.WorkingDirectory = $script:DsApp
        $link.IconLocation = "$($script:DsInstalled),0"
        $link.Description = 'Death Stranding with the co-op launcher'
        $link.Save()
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
}
function Remove-DsShortcut {
    if ((Get-DsShortcutTarget) -eq $script:DsInstalled) {
        Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) $script:DsShortcut)
    }
}
# Invite links: the launcher registers itself (deathstranding-coop.exe --register-protocol, HKCU, no administrator).
function Get-DsInviteCommand { return '"{0}" "%1"' -f $script:DsInstalled }
function Test-DsInviteLinks {
    $command = Get-ItemProperty -LiteralPath "$($script:DsProtocol)\shell\open\command" -ErrorAction SilentlyContinue
    return [bool]($command -and $command.'(default)' -eq (Get-DsInviteCommand))
}
function Set-DsInviteLinks {
    $result = Invoke-DsHidden $script:DsInstalled @('--register-protocol')
    if ($result.ExitCode -ne 0) { throw 'The launcher could not register invite links for this Windows user.' }
}
function Remove-DsInviteLinks {
    if (Test-DsInviteLinks) { Remove-Item -LiteralPath $script:DsProtocol -Recurse }   # only while it points at this install
}
# Windows Firewall (opt-in, one administrator prompt): the game receives the partner's packets, the launcher the lobby's,
# the group server (coop-world) its players'. Private and Public profiles; every other rule is left as it is.
function Get-DsFirewallPlan([string]$game) {
    return @(
        [pscustomobject]@{ Name = 'Death Stranding Co-op (game)';     Program = (Join-Path $game 'ds.exe');                  Protocol = 'UDP' },
        [pscustomobject]@{ Name = 'Death Stranding Co-op (launcher)'; Program = $script:DsInstalled;                         Protocol = 'UDP' },
        [pscustomobject]@{ Name = 'Death Stranding Co-op (server)';   Program = (Join-Path $script:DsApp 'coop-world.exe');  Protocol = 'any' })
}
function Get-DsFirewallRules([string]$game) {
    $policy = $null
    try {
        $policy = New-Object -ComObject HNetCfg.FwPolicy2
        $names = @(Get-DsFirewallPlan $game | ForEach-Object { $_.Name })
        return @($policy.Rules | Where-Object { $names -contains $_.Name })
    } catch { return @() }
    finally { if ($policy) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($policy) } }
}
function Test-DsFirewall([string]$game) {
    $rules = @(Get-DsFirewallRules $game)
    foreach ($want in Get-DsFirewallPlan $game) {
        $profiles = 0
        foreach ($r in $rules) {
            if ($r.Name -eq $want.Name -and $r.ApplicationName -eq $want.Program -and $r.Enabled -and $r.Action -eq 1 -and $r.Direction -eq 1) { $profiles = $profiles -bor $r.Profiles }
        }
        if (($profiles -band 6) -ne 6) { return $false }      # Private = 2, Public = 4
    }
    return $true
}
# Runs netsh; the caller is already elevated (deathstranding-coop-setup.ps1 firewall add|remove <game>).
function Set-DsFirewallNow([string]$game, [bool]$remove) {
    $failed = $false
    foreach ($rule in Get-DsFirewallPlan $game) {
        $delete = @('advfirewall', 'firewall', 'delete', 'rule', "name=$($rule.Name)", "program=$($rule.Program)")
        $had = Invoke-DsHidden 'netsh.exe' $delete
        if ($remove) { continue }
        $add = @('advfirewall', 'firewall', 'add', 'rule', "name=$($rule.Name)", 'dir=in', 'action=allow', "program=$($rule.Program)",
                 "protocol=$($rule.Protocol)", 'profile=private,public', 'enable=yes')
        if ((Invoke-DsHidden 'netsh.exe' $add).ExitCode -ne 0) { $failed = $true }
    }
    if ($failed) { throw 'Windows Firewall could not be changed.' }
}
# From a normal window: one administrator prompt for the whole change.
function Set-DsFirewall([string]$game, [bool]$remove) {
    $script = Join-Path $script:DsPackage 'deathstranding-coop-setup.ps1'
    $verb = if ($remove) { 'remove' } else { 'add' }
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File {0} firewall {1} {2}' -f (ConvertTo-DsArgument $script), $verb, (ConvertTo-DsArgument $game)
    try { $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru }
    catch { throw 'Windows Firewall was not changed: administrator permission was not given.' }
    if ($p.ExitCode -ne 0) { throw 'Windows Firewall could not be changed.' }
}
function Get-DsOptions([string]$game) {
    return [pscustomobject]@{
        desktop_shortcut = ((Get-DsShortcutTarget) -eq $script:DsInstalled)
        allow_hosting    = ($game -and (Test-DsFirewall $game))
        invite_links     = (Test-DsInviteLinks)
    }
}
# The user's own options (no administrator): applied after install, removed by uninstall.
function Set-DsUserOptions([bool]$shortcut, [bool]$invites) {
    if ($shortcut) { Set-DsShortcut } else { Remove-DsShortcut }
    if ($invites) { Set-DsInviteLinks } else { Remove-DsInviteLinks }
}
function Remove-DsUserOptions { Remove-DsShortcut; Remove-DsInviteLinks }
