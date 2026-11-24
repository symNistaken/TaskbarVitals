<#
.SYNOPSIS
    YASB custom-widget backend for Wireless Battery.

.DESCRIPTION
    Prints one small JSON object for a YASB CustomWidget:

        {"label": "...", "short": "...", "tooltip": "...", "count": 1}

    It reads, in this order:

    1. the status.json snapshot the running app keeps in
       %LOCALAPPDATA%\WirelessBattery\status.json (rewritten after every poll,
       so YASB never polls the receivers a second time);
    2. `WirelessBattery.exe --json`, when the app is not running or the
       snapshot is older than three minutes;
    3. legacy `--list` text parsing, for app versions without --json.

    The snapshot already applies the app settings: devices hidden in the app
    (HKCU\Software\WirelessBattery\HiddenDevices) are skipped, the device order
    (DeviceOrder) is respected and HideLastKnownDevices is honoured.
    With no visible devices it prints an object with an empty label, so a
    widget configured with hide_empty: true disappears from the bar instead of
    showing the raw "{data[label]}" template.

.PARAMETER Mode
    lowest (default) shows the device with the lowest battery percentage.
    all shows every visible device. first shows the first one in app order.

.PARAMETER JsonFile
    Test helper: read a status.json document from this file.

.PARAMETER ListFile
    Test helper: read legacy --list output from this file.
#>
[CmdletBinding()]
param(
    [ValidateSet('lowest', 'all', 'first')]
    [string]$Mode = 'lowest',
    [string]$JsonFile,
    [string]$ListFile
)

$ErrorActionPreference = 'Stop'

# --- Appearance -------------------------------------------------------------
# One icon per device type, as reported by the app ("kind" in status.json).
# Nerd Font codepoints; the generic one is used for unknown types.
$KindIcons = @{
    headset  = [char]::ConvertFromUtf32(0xF02CE)  # md-headphones
    mouse    = [char]::ConvertFromUtf32(0xF037D)  # md-mouse
    keyboard = [char]::ConvertFromUtf32(0xF030C)  # md-keyboard
    gamepad  = [char]::ConvertFromUtf32(0xF0297)  # md-gamepad
    phone    = [char]::ConvertFromUtf32(0xF011C)  # md-cellphone
    other    = [char]::ConvertFromUtf32(0xF0FB0)  # md-devices
}
$IconBolt = [char]::ConvertFromUtf32(0xF0E7)      # fa-bolt
$LowPercent = 15
$UseHtmlColors = $true
$Separator = '  '
$TooltipTitle = 'Wireless Battery'
$LogFile = Join-Path $env:TEMP 'wireless-battery-yasb.log'
$TimeoutMs = 20000
$SnapshotMaxAgeSec = 180   # three polls; older means the app is gone

$script:UpdatedUtc = $null

function Write-Log {
    param([string]$Message)
    try {
        $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message
        Add-Content -Path $LogFile -Value $line -Encoding UTF8
    } catch {
        # Logging must never break the widget output.
    }
}

function ConvertTo-JsonString {
    param([AllowNull()][string]$Value)
    if ($null -eq $Value) { return 'null' }
    $text = $Value.Replace('\', '\\').Replace('"', '\"')
    # Escape control characters and everything above ASCII as \uXXXX. The
    # script's stdout encoding can then never corrupt the label glyphs.
    $text = [regex]::Replace($text, '[^\x20-\x7E]', {
            param($match)
            '\u' + ([int][char]$match.Value).ToString('X4')
        })
    return '"' + $text + '"'
}

function Write-RawOutput {
    param([string]$Text)
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $stdout = [Console]::OpenStandardOutput()
    $writer = New-Object System.IO.StreamWriter($stdout, $utf8)
    try {
        $writer.Write($Text)
        $writer.Flush()
    } finally {
        $writer.Dispose()
    }
}

function Find-AppExe {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Wireless Battery\WirelessBattery.exe'),
        (Join-Path $env:ProgramFiles 'Wireless Battery\WirelessBattery.exe')
    )
    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Wireless Battery\WirelessBattery.exe')
    }
    foreach ($path in $candidates) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $uninstallRoots = @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'
    )
    foreach ($root in $uninstallRoots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($entry in (Get-ChildItem $root -ErrorAction SilentlyContinue)) {
            $props = Get-ItemProperty -LiteralPath $entry.PSPath -ErrorAction SilentlyContinue
            if ($props -and $props.DisplayName -like 'Wireless Battery*' -and $props.InstallLocation) {
                $exe = Join-Path $props.InstallLocation 'WirelessBattery.exe'
                if (Test-Path -LiteralPath $exe) { return (Resolve-Path -LiteralPath $exe).Path }
            }
        }
    }

    $command = Get-Command 'WirelessBattery.exe' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

function Invoke-AppProcess {
    param([string]$Exe, [string]$Arguments)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.Arguments = $Arguments
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $psi.StandardErrorEncoding = New-Object System.Text.UTF8Encoding($false)

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    try {
        if (-not $process.Start()) { return $null }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMs)) {
            try { $process.Kill() } catch {}
            Write-Log "ERROR: $Arguments timed out after ${TimeoutMs} ms"
            return $null
        }
        $output = $stdoutTask.Result
        $errors = $stderrTask.Result
        if ($errors -and $errors.Trim()) { Write-Log ($Arguments + ' stderr: ' + $errors.Trim()) }
        return $output
    } catch {
        Write-Log ('ERROR running ' + $Arguments + ': ' + $_.Exception.Message)
        return $null
    } finally {
        $process.Dispose()
    }
}

function Get-SnapshotPath {
    if (-not $env:LOCALAPPDATA) { return $null }
    return (Join-Path $env:LOCALAPPDATA 'WirelessBattery\status.json')
}

# status.json (or --json) to a list of device objects. Returns $null when the
# document is missing, empty or not JSON at all.
function ConvertFrom-StatusJson {
    param([string]$Json)
    if (-not $Json -or -not $Json.Trim()) { return $null }
    try {
        $data = $Json | ConvertFrom-Json
    } catch {
        Write-Log ('ERROR parsing status JSON: ' + $_.Exception.Message)
        return $null
    }
    if ($null -eq $data -or $null -eq $data.devices) { return $null }

    $updated = $null
    if ($data.updated) {
        try {
            $updated = [datetime]::Parse([string]$data.updated, [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind)
        } catch {
            $updated = $null
        }
    }
    $script:UpdatedUtc = $updated

    $devices = New-Object System.Collections.Generic.List[object]
    $index = 0
    foreach ($d in @($data.devices)) {
        if ($null -eq $d) { continue }
        if ($d.connected -eq $false) { continue }

        $percent = -1
        if ($null -ne $d.percent) { $percent = [int]$d.percent }
        $millivolts = 0
        if ($null -ne $d.millivolts) { $millivolts = [int]$d.millivolts }
        $kind = 'other'
        if ($d.kind) { $kind = ([string]$d.kind).ToLowerInvariant() }

        $devices.Add([pscustomobject]@{
                key        = [string]$d.key
                name       = [string]$d.name
                kind       = $kind
                via        = [string]$d.via
                percent    = $percent
                live       = [bool]$d.live
                levelText  = [string]$d.levelText
                detail     = [string]$d.detail
                charging   = [bool]$d.charging
                asleep     = [bool]$d.asleep
                connected  = $true
                millivolts = $millivolts
                index      = $index
            })
        $index++
    }
    return ,$devices.ToArray()
}

# The snapshot the running app keeps current. A stale file means the app was
# closed (or crashed) without deleting it, so it is ignored.
function Read-Snapshot {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $null }
    $age = [datetime]::UtcNow - (Get-Item -LiteralPath $Path).LastWriteTimeUtc
    if ($age.TotalSeconds -gt $SnapshotMaxAgeSec) {
        Write-Log ('snapshot is {0:N0}s old, ignoring' -f $age.TotalSeconds)
        return $null
    }
    try {
        $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    } catch {
        Write-Log ('ERROR reading snapshot: ' + $_.Exception.Message)
        return $null
    }
    $devices = ConvertFrom-StatusJson $raw
    if ($null -ne $devices -and $null -eq $script:UpdatedUtc) {
        $script:UpdatedUtc = (Get-Item -LiteralPath $Path).LastWriteTimeUtc
    }
    # The comma keeps an empty device list an array instead of letting PowerShell
    # flatten it to $null, which would look like "no snapshot" and trigger a
    # pointless --json/--list poll while every device is asleep.
    return ,$devices
}

function Read-AppSettings {
    $result = [pscustomobject]@{
        Hidden        = @()
        Order         = @()
        HideLastKnown = $false
    }
    $key = 'HKCU:\Software\WirelessBattery'
    if (-not (Test-Path $key)) { return $result }
    $props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
    if (-not $props) { return $result }

    if ($null -ne $props.HiddenDevices) {
        $result.Hidden = @($props.HiddenDevices | Where-Object { $_ -ne $null -and $_ -ne '' })
    }
    if ($null -ne $props.DeviceOrder) {
        $result.Order = @($props.DeviceOrder | Where-Object { $_ -ne $null -and $_ -ne '' })
    }
    if ($null -ne $props.HideLastKnownDevices) {
        $result.HideLastKnown = ([int]$props.HideLastKnownDevices -ne 0)
    }
    return $result
}

function ConvertFrom-ListLine {
    param([string]$Line)
    if ([string]::IsNullOrWhiteSpace($Line)) { return $null }
    if ($Line -eq 'No devices with battery info found.') { return $null }

    $line = $Line.TrimEnd("`r", "`n")
    $key = ''

    # The key is the last "   (key)" group.
    $keyMatch = [regex]::Match($line, '\s{2,}\(([^()]*)\)\s*$')
    if ($keyMatch.Success) {
        $key = $keyMatch.Groups[1].Value
        $line = $line.Substring(0, $keyMatch.Index).TrimEnd()
    }

    # name  [via]  status
    $head = [regex]::Match($line, '^(.*?)\s{2,}\[([^\]]*)\]\s{2,}(.*)$')
    if (-not $head.Success) { return $null }

    $name = $head.Groups[1].Value.Trim()
    $via = $head.Groups[2].Value.Trim()
    $status = $head.Groups[3].Value

    $connected = $true
    $asleep = $false
    $charging = $false
    $percent = -1
    $levelText = ''
    $detail = ''
    $millivolts = 0

    # Trailing ", 4200 mV", ", charging" and then the detail "(...)"; strip in
    # reverse order because that is how the app prints them.
    $mvMatch = [regex]::Match($status, ',\s*(\d+)\s*mV\s*$')
    if ($mvMatch.Success) {
        $millivolts = [int]$mvMatch.Groups[1].Value
        $status = $status.Substring(0, $mvMatch.Index).TrimEnd()
    }

    $chargeMatch = [regex]::Match($status, ',\s*charging\s*$', 'IgnoreCase')
    if ($chargeMatch.Success) {
        $charging = $true
        $status = $status.Substring(0, $chargeMatch.Index).TrimEnd()
    }

    $detailMatch = [regex]::Match($status, '\s*\(([^()]*)\)\s*$')
    if ($detailMatch.Success) {
        $detail = $detailMatch.Groups[1].Value.Trim()
        $status = $status.Substring(0, $detailMatch.Index).TrimEnd()
    }

    if ($status -eq 'off / not connected') {
        $connected = $false
    } elseif ($status -eq 'asleep') {
        $asleep = $true
    } elseif ($status -ne 'unknown') {
        $percentMatch = [regex]::Match($status, '(\d{1,3})\s*%')
        if ($percentMatch.Success) {
            $percent = [int]$percentMatch.Groups[1].Value
            if ($percent -gt 100) { $percent = 100 }
        } elseif ($status.Trim()) {
            $levelText = $status.Trim()
        }
    }

    return [pscustomobject]@{
        key        = $key
        name       = $name
        kind       = 'other'   # --list does not expose the device type
        via        = $via
        percent    = $percent
        live       = ($percent -ge 0)
        levelText  = $levelText
        detail     = $detail
        charging   = $charging
        asleep     = $asleep
        connected  = $connected
        millivolts = $millivolts
    }
}

# Legacy path: parse --list and apply the app settings here, because old app
# versions do not have --json/status.json.
function Get-DevicesFromList {
    param([string]$Text)
    if ($null -eq $Text) { return $null }

    $devices = New-Object System.Collections.Generic.List[object]
    $index = 0
    foreach ($line in ($Text -split "`r?`n")) {
        $device = ConvertFrom-ListLine $line
        if ($null -ne $device) {
            $device | Add-Member -NotePropertyName index -NotePropertyValue $index -Force
            $devices.Add($device)
            $index++
        }
    }

    $settings = Read-AppSettings
    $visible = @($devices | Where-Object {
            $_.connected -and
            ($settings.Hidden -notcontains $_.key) -and
            -not ($settings.HideLastKnown -and $_.asleep)
        })

    # App order first, then the order the app listed them in.
    foreach ($device in $visible) {
        $orderIndex = -1
        if ($settings.Order.Count -gt 0) {
            $orderIndex = [array]::IndexOf($settings.Order, $device.key)
        }
        if ($orderIndex -lt 0) { $orderIndex = [int]::MaxValue }
        $device | Add-Member -NotePropertyName orderIndex -NotePropertyValue $orderIndex -Force
    }
    return ,@($visible | Sort-Object -Property orderIndex, index)
}

function Get-Devices {
    param([string]$Exe)

    if ($JsonFile) {
        if (Test-Path -LiteralPath $JsonFile) {
            return ConvertFrom-StatusJson (Get-Content -LiteralPath $JsonFile -Raw -Encoding UTF8)
        }
        Write-Log ('ERROR: JsonFile not found: ' + $JsonFile)
        return $null
    }

    if ($ListFile) {
        if (Test-Path -LiteralPath $ListFile) {
            Write-Log 'source=--list file'
            return Get-DevicesFromList (Get-Content -LiteralPath $ListFile -Raw -Encoding UTF8)
        }
        Write-Log ('ERROR: ListFile not found: ' + $ListFile)
        return $null
    }

    $snapshot = Read-Snapshot (Get-SnapshotPath)
    if ($null -ne $snapshot) {
        Write-Log 'source=snapshot'
        return $snapshot
    }

    if ($Exe) {
        $json = Invoke-AppProcess $Exe '--json'
        $devices = ConvertFrom-StatusJson $json
        if ($null -ne $devices) {
            Write-Log 'source=--json'
            return $devices
        }
        Write-Log 'WARN: --json produced no usable output, falling back to --list'
        $text = Invoke-AppProcess $Exe '--list'
        return Get-DevicesFromList $text
    }

    Write-Log 'ERROR: WirelessBattery.exe not found and no snapshot'
    return $null
}

function Get-DeviceIcon {
    param($Device)
    $kind = [string]$Device.kind
    if ($kind -and $KindIcons.ContainsKey($kind)) { return $KindIcons[$kind] }
    return $KindIcons['other']
}

function Get-DeviceStatusText {
    param($Device)
    if ($Device.percent -ge 0) { return ('{0}%' -f $Device.percent) }
    if ($Device.levelText) { return [string]$Device.levelText }
    return '?'
}

function Get-DeviceColor {
    param($Device)
    if ($Device.charging) { return '#60cdff' }          # charging: blue
    if ($Device.asleep) { return '#9aa0a6' }            # asleep: last known value, gray
    if ($Device.percent -ge 0 -and $Device.percent -le $LowPercent) { return '#ff5a5a' }
    if ($Device.levelText -match 'low|empty|critical') { return '#ff5a5a' }
    if ($Device.percent -lt 0 -and -not $Device.levelText) { return '#9aa0a6' }
    return $null
}

function Format-DeviceLabel {
    param($Device)
    $text = (Get-DeviceIcon $Device) + ' ' + (Get-DeviceStatusText $Device)
    if ($Device.charging) { $text += $IconBolt }
    $color = Get-DeviceColor $Device
    if ($UseHtmlColors -and $color) {
        return '<span style="color:' + $color + ';">' + $text + '</span>'
    }
    return $text
}

function Format-DeviceTooltip {
    param($Device)
    $status = if ($Device.levelText) {
        [string]$Device.levelText
    } elseif ($Device.percent -ge 0) {
        '{0}%' -f $Device.percent
    } else {
        'unknown'
    }

    if ($Device.detail) { $status += ' (' + $Device.detail + ')' }

    $extra = @()
    if ($Device.charging) { $extra += 'charging' }
    if ($Device.asleep) { $extra += 'asleep, last known' }
    if ($Device.millivolts -gt 0) { $extra += ('{0} mV' -f $Device.millivolts) }

    $line = $Device.name + ' ' + [char]0x2014 + ' ' + $status
    if ($extra.Count -gt 0) { $line += ', ' + ($extra -join ', ') }
    if ($Device.via) { $line += ' [' + $Device.via + ']' }
    return $line
}

# YASB leaves "{data[label]}" on screen when the command prints null, so an
# empty result is an object with an empty label instead. With hide_empty: true
# the widget then disappears cleanly.
function New-EmptyWidgetJson {
    return '{"label":"","short":"","tooltip":"","count":0}'
}

function Build-WidgetJson {
    $exe = Find-AppExe
    $devices = Get-Devices $exe
    if ($null -eq $devices) { return New-EmptyWidgetJson }

    $visible = @($devices | Where-Object { $_.connected })
    if ($visible.Count -eq 0) { return New-EmptyWidgetJson }

    $shown = @()
    switch ($Mode) {
        'all' {
            $shown = $visible
        }
        'first' {
            $shown = @($visible[0])
        }
        default {
            $withPercent = @($visible | Where-Object { $_.percent -ge 0 })
            if ($withPercent.Count -gt 0) {
                $lowest = ($withPercent | Measure-Object -Property percent -Minimum).Minimum
                $shown = @($withPercent | Where-Object { $_.percent -eq $lowest } | Select-Object -First 1)
            } else {
                $shown = @($visible[0])
            }
        }
    }

    $label = (($shown | ForEach-Object { Format-DeviceLabel $_ }) -join $Separator)
    $short = (($shown | ForEach-Object { Get-DeviceStatusText $_ }) -join (' ' + [char]0x00B7 + ' '))

    $tooltipLines = @()
    $tooltipLines += ('{0} {1} {2} device(s)' -f $TooltipTitle, [char]0x2014, $visible.Count)
    foreach ($device in $visible) {
        $tooltipLines += '  ' + (Format-DeviceTooltip $device)
    }
    if ($script:UpdatedUtc) {
        $tooltipLines += 'Updated ' + $script:UpdatedUtc.ToLocalTime().ToString('HH:mm')
    } else {
        $tooltipLines += 'Updated ' + (Get-Date -Format 'HH:mm')
    }
    $tooltip = $tooltipLines -join "`n"

    return '{' +
        '"label":' + (ConvertTo-JsonString $label) + ',' +
        '"short":' + (ConvertTo-JsonString $short) + ',' +
        '"tooltip":' + (ConvertTo-JsonString $tooltip) + ',' +
        '"count":' + $visible.Count +
        '}'
}

$json = New-EmptyWidgetJson
try {
    $json = Build-WidgetJson
} catch {
    Write-Log ('ERROR: ' + $_.Exception.Message)
    $json = New-EmptyWidgetJson
}
Write-RawOutput $json
