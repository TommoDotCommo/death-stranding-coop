# Death Stranding Co-op Setup without a window: the verbs every co-op Setup takes (co-op mod standard J9).
# DeathStrandingCoop-Setup.exe passes them on unchanged; the files are the launcher's job (deathstranding-coop.exe --setup,
# launcher/install.cpp), this script adds what uninstall must also take away (shortcut, invite links, firewall rules).
#   DeathStrandingCoop-Setup.exe status [--json] [<game folder>]   without a folder: every install found in the Steam libraries
#   DeathStrandingCoop-Setup.exe install <game folder> --yes
#   DeathStrandingCoop-Setup.exe uninstall <game folder> --yes
#   exit codes: 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running
# Install from here sets no options (shortcut, firewall, invite links): those are the window's ticks.
# `firewall add|remove <game folder>` is the window's elevated step (one administrator prompt); it is not a public verb.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'install-state.ps1')
$words = @($args | ForEach-Object { "$_" })
$verb = if ($words.Count) { $words[0] } else { '' }
$usage = 'Use: status [--json] [<game folder>] | install <game folder> --yes | uninstall <game folder> --yes'
function Out-Result($result) {
    if ($result.Output) { [Console]::Out.Write($result.Output) }
    if ($result.Error) { [Console]::Error.Write($result.Error) }
}
try {
    switch ($verb) {
        'status' {
            $result = Invoke-DsSetup $words      # the same words, verb first
            Out-Result $result
            exit $result.ExitCode
        }
        'install' {
            $result = Invoke-DsSetup $words      # the same words, verb first
            Out-Result $result
            exit $result.ExitCode
        }
        'uninstall' {
            $result = Invoke-DsSetup $words      # the same words, verb first
            Out-Result $result
            if ($result.ExitCode -eq $script:DsExitUsage -or $result.ExitCode -eq $script:DsExitRunning) { exit $result.ExitCode }
            $code = $result.ExitCode
            Remove-DsUserOptions
            $game = @($words | Select-Object -Skip 1 | Where-Object { $_ -notlike '--*' })[0]
            if ($game -and @(Get-DsFirewallRules $game).Count) {
                try { Set-DsFirewallNow $game $true }
                catch {
                    [Console]::Error.WriteLine('The Windows Firewall rules for co-op are still there: removing them needs administrator permission. Run Setup and choose Uninstall, or run this from an administrator prompt.')
                    $code = $script:DsExitFailed
                }
            }
            exit $code
        }
        'firewall' {
            if ($words.Count -ne 3 -or $words[1] -notin 'add', 'remove') { [Console]::Error.WriteLine($usage); exit $script:DsExitUsage }
            Set-DsFirewallNow $words[2] ($words[1] -eq 'remove')
            exit 0
        }
        default { [Console]::Error.WriteLine($usage); exit $script:DsExitUsage }
    }
} catch {
    [Console]::Error.WriteLine("$($_.Exception.Message)")
    exit $script:DsExitFailed
}
