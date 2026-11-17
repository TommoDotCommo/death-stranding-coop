# Death Stranding Co-op Setup: the window (co-op mod standard G1 to G4). DeathStrandingCoop-Setup.exe starts it.
# Every install found in the Steam libraries is a card with its build check; Browse adds a folder. One button says what
# it will do (Install, Update, Repair, Up to date, Needs attention); after an install it becomes Play. Uninstall takes the
# co-op files, the shortcut, the invite links and the firewall rules away and keeps the co-op saves.
# The file decisions are the launcher's (deathstranding-coop.exe --setup ..., launcher/install.cpp); this window draws them.
param([string]$SetupExe = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
. (Join-Path $PSScriptRoot 'install-state.ps1')

$version = Get-DsVersion
$form = New-Object Windows.Forms.Form
$form.Text = "Death Stranding Co-op Setup $version"
$form.ClientSize = New-Object Drawing.Size(760, 560)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object Drawing.Font('Segoe UI', 9)
if ($SetupExe -and (Test-Path -LiteralPath $SetupExe)) { try { $form.Icon = [Drawing.Icon]::ExtractAssociatedIcon($SetupExe) } catch { } }

function New-Control($type, $x, $y, $w, $h, $text = '') {
    $c = New-Object "Windows.Forms.$type"
    $c.Location = New-Object Drawing.Point($x, $y)
    $c.Size = New-Object Drawing.Size($w, $h)
    if ($text) { $c.Text = $text }
    $form.Controls.Add($c)
    return $c
}
$title = New-Control Label 16 12 728 24 'Add co-op to DEATH STRANDING DIRECTOR''S CUT'
$title.Font = New-Object Drawing.Font('Segoe UI', 12, [Drawing.FontStyle]::Bold)
[void](New-Control Label 16 40 728 34 ("Setup found these game folders. Co-op adds one file (avrt.dll) to the game folder and keeps its own saves " +
    "in %LOCALAPPDATA%\DeathStrandingCoop; your ordinary saves are never touched. Only Steam build 13300582 is supported."))
$list = New-Control ListView 16 80 728 150
$list.View = 'Details'; $list.FullRowSelect = $true; $list.HideSelection = $false; $list.MultiSelect = $false
[void]$list.Columns.Add('Game folder', 430); [void]$list.Columns.Add('Build', 140); [void]$list.Columns.Add('Co-op', 150)
$browse = New-Control Button 16 236 120 28 'Browse...'
$refresh = New-Control Button 142 236 120 28 'Look again'
$details = New-Control TextBox 16 272 728 120
$details.Multiline = $true; $details.ReadOnly = $true; $details.ScrollBars = 'Vertical'
$options = New-Control GroupBox 16 400 728 100 'Options'
function New-Option($y, $text) {
    $c = New-Object Windows.Forms.CheckBox
    $c.Location = New-Object Drawing.Point(12, $y); $c.Size = New-Object Drawing.Size(700, 22); $c.Text = $text
    $options.Controls.Add($c); return $c
}
$optShortcut = New-Option 20 'Desktop shortcut to the co-op launcher'
$optInvites = New-Option 44 'Open invite links (deathstranding-coop://) in the co-op launcher'
$optFirewall = New-Option 68 'Allow hosting through Windows Firewall, Private and Public networks (asks for administrator permission once)'
$optShortcut.Checked = $true; $optInvites.Checked = $true; $optFirewall.Checked = $false     # the firewall rule is opt-in
$primary = New-Control Button 524 516 120 32 'Install'
$uninstall = New-Control Button 398 516 120 32 'Uninstall'
$close = New-Control Button 650 516 94 32 'Close'
$form.AcceptButton = $primary; $form.CancelButton = $close
$status = New-Control Label 16 522 370 24 ''

$script:cards = @()
$script:played = $false        # the last screen's button is Play (G4)
$script:applying = $false

function Get-Selected { if ($list.SelectedIndices.Count) { return $script:cards[$list.SelectedIndices[0]] } return $null }
function Get-BuildText($card) {
    switch ($card.state) { 'not-a-game-folder' { return 'no ds.exe' } 'unsupported-build' { return 'unsupported build' } default { return $card.build } }
}
function Get-StateText($card) {
    switch ($card.state) {
        'not-installed' { return 'not installed' }
        'up-to-date' { return "installed ($($card.installed_version))" }
        'update-available' { return "update to $($card.version)" }
        'needs-repair' { return 'needs repair' }
        'installed-by-mod-manager' { return 'installed by a mod manager' }
        default { return 'needs attention' }
    }
}
function Show-Cards([string]$select = '') {
    $script:cards = @(Get-DsCards $select)
    $list.Items.Clear()
    foreach ($c in $script:cards) {
        $item = New-Object Windows.Forms.ListViewItem("$($c.game)")
        [void]$item.SubItems.Add((Get-BuildText $c)); [void]$item.SubItems.Add((Get-StateText $c))
        [void]$list.Items.Add($item)
    }
    $index = -1
    for ($i = 0; $i -lt $script:cards.Count; $i++) { if ($select -and $script:cards[$i].game -eq $select) { $index = $i } }
    if ($index -lt 0) { for ($i = 0; $i -lt $script:cards.Count; $i++) { if ($script:cards[$i].installed_by -and $index -lt 0) { $index = $i } } }
    if ($index -lt 0 -and $script:cards.Count) { $index = 0 }
    if ($index -ge 0) { $list.Items[$index].Selected = $true }
    Update-Screen
}
function Update-Screen {
    $card = Get-Selected
    $script:applying = $true
    if (-not $card) {
        $details.Text = 'No DEATH STRANDING DIRECTOR''S CUT was found in your Steam libraries. Choose Browse and pick the folder that holds ds.exe.'
        $primary.Text = 'Install'; $primary.Enabled = $false; $uninstall.Enabled = $false
        $script:applying = $false; return
    }
    $installed = $card.state -in 'up-to-date', 'update-available', 'needs-repair', 'installed-by-mod-manager' -or "$($card.installed_by)" -eq 'setup'
    $text = switch ($card.state) {
        'not-installed' { 'Co-op is not installed in this game folder yet. Choose Install.' }
        'up-to-date' { "Co-op $($card.installed_version) is installed and up to date. Choose Play to start the co-op launcher." }
        'update-available' { "Co-op $($card.version) is ready to replace the installed version. Choose Update." }
        'needs-repair' { 'Some co-op files are missing or were installed without Setup. Choose Repair.' }
        'installed-by-mod-manager' { 'Co-op was installed by a mod manager. Update or remove it there; Setup leaves its files alone.' }
        'unsupported-build' { 'This is not Steam build 13300582 of the Director''s Cut, so co-op would stay off. In Steam: Properties, Installed Files, Verify; and Betas: None. Then choose Look again.' }
        'not-a-game-folder' { 'This folder has no ds.exe. Choose the DEATH STRANDING DIRECTOR''S CUT folder.' }
        default { 'Setup cannot install here without changing a file that is not the co-op mod''s, so it changes nothing. What to do:' }
    }
    # A fresh install lists every file as missing: that reads like an error to a new player, so only other states list them.
    $problems = @(if ($card.state -ne 'not-installed') { $card.problems | Where-Object { $_ } })
    $details.Text = $text + $(if ($problems.Count) { "`r`n`r`n" + ($problems -join "`r`n") } else { '' }) + "`r`n`r`nFolder: $($card.game)"
    if ($card.state -in 'up-to-date', 'installed-by-mod-manager' -or $script:played) { $primary.Text = 'Play'; $primary.Enabled = (Test-Path -LiteralPath $script:DsInstalled) }
    else { $primary.Text = "$($card.button)"; $primary.Enabled = $card.button -in 'Install', 'Update', 'Repair' }
    $uninstall.Enabled = $installed -and $card.state -ne 'installed-by-mod-manager'
    if ($installed) {
        $o = Get-DsOptions $card.game
        $optShortcut.Checked = $o.desktop_shortcut; $optInvites.Checked = $o.invite_links; $optFirewall.Checked = $o.allow_hosting
    }
    $script:applying = $false
}
function Invoke-Busy([scriptblock]$work) {
    $form.UseWaitCursor = $true; $form.Enabled = $false
    try { & $work } catch { [void][Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'Death Stranding Co-op Setup', 'OK', 'Warning') }
    finally { $form.Enabled = $true; $form.UseWaitCursor = $false }
}
function Get-Failure($result, [string]$what) {
    switch ($result.ExitCode) {
        4 { return "The game is running. Close DEATH STRANDING, then choose $what again." }
        3 { return 'This game build is not supported (Steam build 13300582 only). Nothing was changed.' }
        default { $e = "$($result.Error)".Trim(); if (-not $e) { $e = "Setup could not $($what.ToLower()) (exit $($result.ExitCode))." }; return $e }
    }
}
function Set-Firewall([string]$game, [bool]$on) {
    if ($on -eq (Test-DsFirewall $game)) { return }
    Set-DsFirewall $game (-not $on)
}

$list.Add_SelectedIndexChanged({ if (-not $script:applying) { $script:played = $false; Update-Screen } })
$browse.Add_Click({
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Choose the DEATH STRANDING DIRECTOR''S CUT folder (the one that holds ds.exe)'
    if ($dialog.ShowDialog($form) -eq 'OK') { Invoke-Busy { Show-Cards $dialog.SelectedPath } }
})
$refresh.Add_Click({ $card = Get-Selected; Invoke-Busy { Show-Cards $(if ($card) { $card.game } else { '' }) } })
$primary.Add_Click({
    $card = Get-Selected; if (-not $card) { return }
    if ($primary.Text -eq 'Play') {
        Start-Process -FilePath $script:DsInstalled -WorkingDirectory $script:DsApp
        $form.Close(); return
    }
    $verb = $primary.Text
    Invoke-Busy {
        $status.Text = "$verb..."
        $result = Invoke-DsSetup @('install', $card.game, '--yes')
        if ($result.ExitCode -ne 0) { $status.Text = ''; throw (Get-Failure $result $verb) }
        Set-DsUserOptions $optShortcut.Checked $optInvites.Checked
        $note = ''
        try { Set-Firewall $card.game $optFirewall.Checked } catch { $note = " $($_.Exception.Message)" }
        $script:played = $true
        $status.Text = "Done.$note"
        Show-Cards $card.game
    }
})
$uninstall.Add_Click({
    $card = Get-Selected; if (-not $card) { return }
    $ask = "Remove co-op from this game folder?`r`n`r`n$($card.game)`r`n`r`nThe shortcut, the invite links and the firewall rules go too. " +
           'Your co-op saves and settings stay in %LOCALAPPDATA%\DeathStrandingCoop.'
    if ([Windows.Forms.MessageBox]::Show($form, $ask, 'Uninstall', 'YesNo', 'Question') -ne 'Yes') { return }
    Invoke-Busy {
        $result = Invoke-DsSetup @('uninstall', $card.game, '--yes', '--json')
        if ($result.ExitCode -eq 4 -or $result.ExitCode -eq 2) { throw (Get-Failure $result 'Uninstall') }
        Remove-DsUserOptions
        $note = ''
        if (@(Get-DsFirewallRules $card.game).Count) { try { Set-DsFirewall $card.game $true } catch { $note = "`r`n`r`n$($_.Exception.Message)" } }
        $report = $null; try { $report = $result.Output | ConvertFrom-Json } catch { }
        $kept = @($report.kept) + @($report.foreign) | Where-Object { $_ }
        $message = 'Co-op was removed. The game starts as before.'
        if ($kept.Count) { $message = "Co-op was removed, except these files, which Setup left alone:`r`n" + ($kept -join "`r`n") }
        $script:played = $false
        [void][Windows.Forms.MessageBox]::Show($form, $message + $note, 'Uninstall', 'OK', 'Information')
        Show-Cards $card.game
    }
})
foreach ($option in $optShortcut, $optInvites, $optFirewall) {
    $option.Add_CheckedChanged({
        if ($script:applying) { return }
        $changed = $this
        $card = Get-Selected
        if (-not $card -or -not (Test-Path -LiteralPath $script:DsInstalled)) { return }    # not installed yet: applied by Install
        Invoke-Busy {
            if ($changed -eq $optFirewall) { Set-Firewall $card.game $optFirewall.Checked }
            else { Set-DsUserOptions $optShortcut.Checked $optInvites.Checked }
        }
        Update-Screen
    })
}
$close.Add_Click({ $form.Close() })
$form.Add_Shown({ Invoke-Busy { Show-Cards } })
[void]$form.ShowDialog()
