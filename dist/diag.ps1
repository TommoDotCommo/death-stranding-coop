# Death Stranding Co-op diagnostics. Embedded in the launcher; reads only, uploads nothing.
# powershell -NoProfile -File diag.ps1 -GameDir '<game folder>' -ReportFile '<text report>'
param(
    [string]$GameDir = '',
    [string]$DataDir = '',
    [string]$Version = 'unknown',
    [string]$ReportFile = '',
    [string]$OutDir = '',
    [string]$ResultFile = '',
    [ValidateRange(1, 1024)][int]$MaxLogMB = 60,
    [switch]$NoOpen,
    # Game-free Windows fixture: skip hardware/network queries, use the supplied fake folders.
    [switch]$Fixture
)
$ErrorActionPreference = 'Stop'
$stage = $null
$zip = $null
$complete = $false
function Finish([string]$message) {
    if ($ResultFile) { [IO.File]::WriteAllText($ResultFile, $message, [Text.Encoding]::UTF8) }
}
try {
    if (-not $DataDir) { $DataDir = Join-Path $env:LOCALAPPDATA 'DeathStrandingCoop' }
    if (-not $OutDir) { $OutDir = [Environment]::GetFolderPath('Desktop') }
    if (-not $OutDir -or -not (Test-Path -LiteralPath $OutDir -PathType Container)) { throw 'Desktop/output folder is unavailable.' }
    $policy = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'diag-rules.json') -Raw | ConvertFrom-Json
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('DeathStrandingCoop-diag-' + [Guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $stage)
    $problems = New-Object Collections.ArrayList
    $chains = @{}
    $tokens = @{}
    $literals = @{ profile = $env:USERPROFILE; user = [Environment]::UserName; machine = [Environment]::MachineName }
    $secrets = New-Object Collections.ArrayList
    $utf8 = New-Object Text.UTF8Encoding($false)
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
public static class DsDiag {
    public static byte[] Read(string path, long limit) {
        using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete)) {
            if (f.Length > limit) throw new IOException("file exceeds remaining size limit");
            using (var m = new MemoryStream()) {
                byte[] buffer = new byte[65536]; int n;
                while ((n = f.Read(buffer, 0, buffer.Length)) > 0) {
                    if (m.Length + n > limit) throw new IOException("growing file exceeds remaining size limit");
                    m.Write(buffer, 0, n);
                }
                return m.ToArray();
            }
        }
    }
    // Agreed alpha format: FNV-1a/32 of "<n> <utc ms> <text>", continued per file.
    public static string Chain(byte[] data, bool expected) {
        string text = Encoding.UTF8.GetString(data);
        if (!expected && !Regex.IsMatch(text, @"^\d+ \d+ [0-9a-f]{8} ")) return "not-present|0||False||";
        uint h = 2166136261; int lines = 0; bool clean = false; string first = "", last = "";
        int pos = 0;
        while (pos < text.Length) {
            int end = text.IndexOf('\n', pos); if (end < 0) break;
            string line = text.Substring(pos, end - pos); pos = end + 1;
            var m = Regex.Match(line, @"^(\d+) (\d+) ([0-9a-f]{8}) (.*)$");
            if (!m.Success || m.Groups[1].Value != lines.ToString()) return "broken|"+lines+"|malformed or missing line|"+clean+"|"+first+"|"+last;
            foreach (byte b in Encoding.UTF8.GetBytes(m.Groups[1].Value+" "+m.Groups[2].Value+" "+m.Groups[4].Value)) { unchecked { h = (h ^ b) * 16777619; } }
            if (m.Groups[3].Value != h.ToString("x8")) return "broken|"+lines+"|chain mismatch|"+clean+"|"+first+"|"+last;
            if (lines == 0) first = m.Groups[2].Value;
            last = m.Groups[2].Value; clean = m.Groups[4].Value == "session end: clean exit"; lines++;
        }
        return "ok|"+lines+"||"+clean+"|"+first+"|"+last;
    }
    public static string Decode(byte[] data) {
        if (data.Length >= 2 && data[0] == 255 && data[1] == 254) return Encoding.Unicode.GetString(data, 2, data.Length - 2);
        if (data.Length >= 2 && data[0] == 254 && data[1] == 255) return Encoding.BigEndianUnicode.GetString(data, 2, data.Length - 2);
        if (data.Length >= 4 && data[1] == 0 && data[3] == 0) return Encoding.Unicode.GetString(data);
        return Encoding.UTF8.GetString(data).TrimStart('\ufeff');
    }
}
'@
    function Redact([string]$text, [bool]$fixed = $false) {
        foreach ($secret in $secrets) {
            $text = [regex]::Replace($text, [regex]::Escape($secret), { param($m) if ($fixed) { return '#' * $m.Length }; return '<secret>' })
        }
        foreach ($rule in $policy.rules) {
            $pattern = $rule.pattern
            if ($rule.literal) {
                $literal = $literals[$rule.literal]
                if (-not $literal) { continue }
                $pattern = '(?i)' + [regex]::Escape($literal)
            }
            $text = [regex]::Replace($text, $pattern, {
                param($m)
                if ($fixed) { return '#' * $m.Length }
                if ($rule.token) {
                    $key = $rule.token + ':' + $m.Value.ToLowerInvariant()
                    if (-not $tokens.ContainsKey($key)) { $tokens[$key] = 1 + @($tokens.Keys | Where-Object { $_.StartsWith($rule.token + ':') }).Count }
                    return $rule.token + '-' + $tokens[$key]
                }
                return $m.Result($rule.replacement)
            })
        }
        return $text
    }
    function MaskBinary([byte[]]$bytes) {
        $text = Redact ($latin1.GetString($bytes)) $true
        # Replace embedded UTF-16LE strings in place, without decoding arbitrary binary as UTF-16.
        $text = [regex]::Replace($text, '(?:[\x20-\x7e]\x00){2,}', {
            param($m)
            $wide = [Text.Encoding]::Unicode.GetString($latin1.GetBytes($m.Value))
            return $latin1.GetString([Text.Encoding]::Unicode.GetBytes((Redact $wide $true)))
        })
        $dumpLiterals = @($literals.Values) + @($secrets)
        foreach ($value in $literals.Values) { if ($value -and $value.Length -ge 4) { $dumpLiterals += $value.Substring(0, 3) } }
        foreach ($literal in $dumpLiterals) {
            if (-not $literal) { continue }
            $raw8 = $latin1.GetString($utf8.GetBytes($literal))
            $text = [regex]::Replace($text, [regex]::Escape($raw8), { param($m) return '#' * $m.Length }, 'IgnoreCase')
            $raw = $latin1.GetString([Text.Encoding]::Unicode.GetBytes($literal))
            $text = [regex]::Replace($text, [regex]::Escape($raw), { param($m) return ('#' + [char]0) * ($m.Length / 2) }, 'IgnoreCase')
        }
        return ,($latin1.GetBytes($text))
    }
    function Stage([string]$rel, [string]$text) {
        $path = Join-Path $stage $rel
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $path))
        [IO.File]::WriteAllText($path, (Redact $text), $utf8)
    }
    function Section([string]$name, [scriptblock]$body) {
        try { & $body } catch { [void]$problems.Add((Redact ("${name}: " + $_.Exception.Message))) }
    }
    function Query([string]$name, [scriptblock]$body) {
        try { & $body } catch { "${name} unavailable: $($_.Exception.Message)" }
    }
    # Config is an explicit allowlist. Never collect player.key, save data, or the world database.
    $configs = @('launcher.ini', 'mod.json', 'redirect-data\mod\mod.json')
    foreach ($rel in $configs) {
        $src = Join-Path $DataDir $rel
        if (Test-Path -LiteralPath $src -PathType Leaf) {
            $content = [DsDiag]::Decode([DsDiag]::Read($src, 1MB))
            foreach ($m in [regex]::Matches($content, '(?im)["'']?(?:session|secret|group_secret|relay_key|strand_token|token|password|api_key|private_key)["'']?\s*[:=]\s*["'']?([^"'',\s}]+)')) {
                if ($m.Groups[1].Value.Length -ge 4) { [void]$secrets.Add($m.Groups[1].Value) }
            }
        }
    }
    foreach ($rel in $configs) {
        $src = Join-Path $DataDir $rel
        if (Test-Path -LiteralPath $src -PathType Leaf) { Section $rel { Stage ('config\' + $rel) ([DsDiag]::Decode([DsDiag]::Read($src, 1MB))) } }
    }
    # Collect both generations, strictly newest-first by mtime, with no crash bypass of the byte limit.
    $candidates = @()
    foreach ($root in @(@('logs', 'logs'), @('redirect-data', 'legacy\redirect'), @('redirect-data\mod', 'legacy\mod'))) {
        $dir = Join-Path $DataDir $root[0]
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        foreach ($f in Get-ChildItem -LiteralPath $dir -File) {
            if ($f.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            if ($root[0] -eq 'logs') { $allowed = $f.Name -match '^\d{8}-\d{6}_(game|proxy|launcher|world)_p\d+\.(log|crash|dmp|flight-\d+\.txt)$' }
            else { $allowed = $f.Name -match '^(mod-|redirect-|presence-|crash-).+\.(log|jsonl|dmp|crash)$' }
            if ($allowed) { $candidates += [pscustomobject]@{ File = $f; Rel = $root[1] + '\' + $f.Name } }
        }
    }
    $budget = [int64]$MaxLogMB * 1MB
    foreach ($c in $candidates | Sort-Object @{Expression={$_.File.LastWriteTimeUtc};Descending=$true}, Rel) {
        $rel = Redact $c.Rel
        if ($c.File.Length -gt $budget) { [void]$problems.Add("size limit: omitted $rel"); continue }
        Section $rel {
            $bytes = [DsDiag]::Read($c.File.FullName, $budget)
            $script:budget -= $bytes.Length
            $dest = Join-Path $stage $rel
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent $dest))
            if ($c.File.Extension -eq '.dmp') { [IO.File]::WriteAllBytes($dest, (MaskBinary $bytes)) }
            else {
                if ($c.File.Extension -eq '.log') { $chains[$rel] = [DsDiag]::Chain($bytes, $c.Rel.StartsWith('logs\')) }
                Stage $rel ([DsDiag]::Decode($bytes))
            }
        }
    }
    if ($ReportFile -and (Test-Path -LiteralPath $ReportFile -PathType Leaf)) { Stage 'report.txt' ([DsDiag]::Decode([DsDiag]::Read($ReportFile, 256KB))) }
    else { Stage 'report.txt' 'Text report unavailable. Run deathstranding-coop.exe --diagnostics, or use About > Send diagnostics.' }
    Stage 'system.txt' 'System queries unavailable (see manifest problems).'
    Stage 'network.txt' 'Network queries unavailable (see manifest problems).'
    Section 'game folder' {
        $lines = @('Mod-related files only; no game files copied.')
        if ($GameDir -and (Test-Path -LiteralPath $GameDir -PathType Container)) {
            foreach ($f in Get-ChildItem -LiteralPath $GameDir -File | Where-Object { $_.Name -match '^(avrt(\.chain)?\.dll|coop-mod.*\.json|deathstranding-coop\.exe|coop-world\.exe)$' }) {
                $lines += '{0}  {1} bytes  sha256 {2}' -f $f.Name, $f.Length, (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        } else { $lines += 'Game folder unknown or unavailable.' }
        Stage 'game-folder.txt' ($lines -join "`n")
    }
    if (-not $Fixture) {
        Section 'system' {
            $lines = @("mod version: $Version", "created UTC: $([DateTime]::UtcNow.ToString('o'))")
            $os = Get-CimInstance Win32_OperatingSystem
            $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
            $lines += "windows: $($os.Caption) $($os.Version) build $($os.BuildNumber) $($os.OSArchitecture)"
            $lines += "cpu: $($cpu.Name), $($cpu.NumberOfCores) cores / $($cpu.NumberOfLogicalProcessors) threads"
            $lines += 'memory: {0:N1} GB, {1:N1} GB free' -f ($os.TotalVisibleMemorySize / 1MB), ($os.FreePhysicalMemory / 1MB)
            foreach ($g in Get-CimInstance Win32_VideoController) { $lines += "graphics: $($g.Name), driver $($g.DriverVersion) ($($g.DriverDate))" }
            $exe = if ($GameDir) { Join-Path $GameDir 'ds.exe' } else { '' }
            if ($exe -and (Test-Path -LiteralPath $exe -PathType Leaf)) {
                $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
                $ok = $hash -eq '9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b'
                $lines += "ds.exe: sha256 $hash; supported build 13300582: $ok"
            } else { $lines += 'ds.exe: unknown (not found)' }
            $lines += '== overlay/capture programs running'
            $watch = @('discord','obs64','RTSS','MSIAfterburner','GameBar','GameBarFTServer','NVIDIA Share','nvsphelper64','RadeonSoftware','overwolf','Medal','steam','steamwebhelper')
            $lines += @(Get-Process | Where-Object { $watch -contains $_.ProcessName } | Select-Object -ExpandProperty ProcessName)
            $lines += '== Windows Error Reporting / application errors for ds.exe (14 days)'
            try {
                $events = @(Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddDays(-14); Id=1000,1001} -ErrorAction Stop | Where-Object { $_.Message -match '(?i)\bds\.exe\b' } | Select-Object -First 8)
                foreach ($e in $events) { $lines += "$($e.TimeCreated.ToUniversalTime().ToString('o')) id $($e.Id) $($e.Message)" }
                if (-not $events.Count) { $lines += 'none' }
            } catch { $lines += "unavailable or no entries: $($_.Exception.Message)" }
            $lines += '== Defender detections touching mod files'
            try {
                $detections = @(Get-MpThreatDetection -ErrorAction Stop | Where-Object { ($_.Resources -join ' ') -match '(?i)DeathStrandingCoop|avrt\.dll|deathstranding-coop|coop-world|redirect-data[\\/]mod' } | Select-Object -First 8)
                foreach ($d in $detections) { $lines += "$($d.InitialDetectionTime) threat $($d.ThreatID): $($d.Resources -join ', ')" }
                if (-not $detections.Count) { $lines += 'none' }
            } catch { $lines += "unavailable: $($_.Exception.Message)" }
            Stage 'system.txt' ($lines -join "`n")
        }
        Section 'network' {
            $lines = @('== adapters and addresses')
            $lines += Query 'adapters' { Get-NetAdapter | Select-Object Name,InterfaceDescription,Status,LinkSpeed,MacAddress | Format-Table | Out-String }
            $lines += Query 'addresses' { Get-NetIPAddress -AddressFamily IPv4 | Select-Object InterfaceAlias,IPAddress,PrefixLength | Format-Table | Out-String }
            $lines += Query 'network profiles' { Get-NetConnectionProfile | Select-Object InterfaceAlias,NetworkCategory,IPv4Connectivity | Format-Table | Out-String }
            $lines += '== firewall rules for ds.exe / the mod'
            $lines += Query 'firewall' {
                $filters = @(Get-NetFirewallApplicationFilter | Where-Object { $_.Program -match '(?i)[\\/](ds|deathstranding-coop|coop-world)\.exe$' })
                foreach ($filter in $filters) { $filter | Get-NetFirewallRule | Select-Object Direction,Action,Profile,Enabled | Format-Table | Out-String }
                if (-not $filters.Count) { 'no matching rules' }
            }
            $server = $null
            $config = Join-Path $DataDir 'redirect-data\mod\mod.json'
            if (Test-Path -LiteralPath $config) {
                $cfg = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
                if ($cfg.server) { $server = [string]$cfg.server }
                elseif ($cfg.peer) { $server = [string]$cfg.peer }
                elseif ($cfg.online_server) { $server = ([uri]$cfg.online_server).Host }
            }
            $lines += '== ICMP ping to configured server (failure may mean ICMP is blocked)'
            if ($server) {
                $ping = New-Object Net.NetworkInformation.Ping
                try { for ($i=0; $i -lt 3; $i++) { $p = $ping.Send($server, 800); $lines += "$server $($p.Status) $($p.RoundtripTime) ms" } }
                catch { $lines += "ping unavailable: $($_.Exception.Message)" }
                finally { $ping.Dispose() }
            } else { $lines += 'no configured server' }
            $lines += '== Tailscale (no accounts or device names collected)'
            $ts = Get-Command tailscale.exe -ErrorAction SilentlyContinue
            if (-not $ts -and $env:ProgramFiles) { $ts = Get-Command (Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe') -ErrorAction SilentlyContinue }
            if ($ts) {
                try {
                $status = ((& $ts.Source status --json 2>$null) -join "`n") | ConvertFrom-Json
                $peers = @($status.Peer.PSObject.Properties | ForEach-Object { $_.Value })
                $lines += "backend: $($status.BackendState); peers: $($peers.Count); online: $(@($peers | Where-Object { $_.Online }).Count)"
                foreach ($peer in $peers | Where-Object { $_.TailscaleIPs -contains $server }) { $lines += "configured peer: online $($peer.Online); direct $($peer.CurAddr); DERP $($peer.Relay); tx $($peer.TxBytes); rx $($peer.RxBytes)" }
                } catch { $lines += "Tailscale unavailable: $($_.Exception.Message)" }
            } else { $lines += 'not installed' }
            Stage 'network.txt' ($lines -join "`n")
        }
    }
    Stage 'README.txt' @'
Death Stranding Co-op diagnostics. Nothing uploaded. No saves or game files copied.
New logs are in logs/, older redirect-data logs in legacy/. Configs are masked.
Names, profile paths, IPv4 addresses, MACs, Steam IDs, emails and secrets are masked.
Dump strings are overwritten in place with same-length text; dumps may contain other memory.
manifest.json hashes all other files (a manifest cannot hash itself). Chain results describe
original logs before redaction. Size limit counts input log/dump bytes, newest first;
files that do not fit are omitted and recorded. There is no crash-file budget exception.
'@
    $entries = @()
    foreach ($f in Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName) {
        $rel = $f.FullName.Substring($stage.Length + 1)
        $entry = [ordered]@{file=($rel -replace '\\','/'); bytes=$f.Length; sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        if ($chains.ContainsKey($rel)) {
            $c = $chains[$rel].Split('|')
            $entry['chain'] = [ordered]@{status=$c[0]; lines=[int]$c[1]; problem=$c[2]; clean_exit=($c[3] -eq 'True'); first_utc_ms=$c[4]; last_utc_ms=$c[5]}
        }
        $entries += $entry
    }
    $manifest = [ordered]@{format=1; product='Death Stranding Co-op'; mod_version=(Redact $Version); created_utc=[DateTime]::UtcNow.ToString('o'); max_log_bytes=([int64]$MaxLogMB*1MB); collected_log_bytes=([int64]$MaxLogMB*1MB-$budget); problems=@($problems); files=$entries}
    [IO.File]::WriteAllText((Join-Path $stage 'manifest.json'), ($manifest | ConvertTo-Json -Depth 8), $utf8)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = Join-Path $OutDir ('DeathStrandingCoop-diagnostics-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '.zip')
    $archive = [IO.Compression.ZipFile]::Open($zip, 'Create')
    try {
        foreach ($f in Get-ChildItem -LiteralPath $stage -Recurse -File) {
            $rel = $f.FullName.Substring($stage.Length + 1) -replace '\\','/'
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
    $complete = $true
    Finish "OK $zip"
    if (-not $NoOpen) { Start-Process explorer.exe -ArgumentList "/select,`"$zip`"" }
} catch {
    Finish ('ERR Diagnostics could not be saved: ' + $_.Exception.Message)
    exit 1
} finally {
    if ($stage -and (Test-Path -LiteralPath $stage)) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if ($zip -and -not $complete -and (Test-Path -LiteralPath $zip)) { Remove-Item -LiteralPath $zip -Force }
}
