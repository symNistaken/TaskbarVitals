param(
    [string]$SourcePath = '',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $root 'site\devices.json'
}

function Read-GitFileUtf8([string]$repoRoot, [string]$revSpec) {
    $prevEncoding = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
        $lines = & git -C $repoRoot show $revSpec
        if ($LASTEXITCODE -ne 0) {
            throw "git show $revSpec failed."
        }
        return ($lines -join "`n")
    } finally {
        [Console]::OutputEncoding = $prevEncoding
    }
}

function Get-MatrixLines([string]$markdownText) {
    $allLines = $markdownText -split "`r?`n"
    $inMatrix = $false
    $rows = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $allLines) {
        if (-not $inMatrix) {
            if ($line -match '^\|\s*Vendor\s*/\s*Family\s*\|') {
                $inMatrix = $true
            }
            continue
        }
        if ($line -match '^\|[\s:-]+\|') {
            continue
        }
        if ($line -notmatch '^\|') {
            break
        }
        $rows.Add($line)
    }
    return $rows
}

function Decode-Entities([string]$text) {
    $text = $text -replace '&amp;', '&'
    $text = $text -replace '&lt;', '<'
    $text = $text -replace '&gt;', '>'
    $text = $text -replace '&quot;', '"'
    $text = $text -replace '&nbsp;', ' '
    return $text
}

function Clean-MarkdownCell([string]$text) {
    $s = Decode-Entities $text
    $s = $s -replace '<br\s*/?>', "`n"
    $s = [regex]::Replace($s, '\[([^\]]+)\]\(([^)]+)\)', '$1: $2')
    $s = [regex]::Replace($s, '\*\*([^*]+?)\*\*', '$1')
    $s = [regex]::Replace($s, '(?<=^|[\s(])\*([^*\r\n]+?)\*(?=[\s).,;:]|$)', '$1')
    $s = $s -replace '`', ''
    $lines = foreach ($part in ($s -split "`n")) {
        $trimmed = ([regex]::Replace($part, '[ \t]{2,}', ' ')).Trim()
        if ($trimmed.Length -gt 0) { $trimmed }
    }
    return ($lines -join "`n")
}

function Extract-VidPids([string]$rawModels) {
    $s = Decode-Entities $rawModels
    $result = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)

    $addVidPid = {
        param([string]$vid, [string]$pidHex)
        $vp = ($vid.ToUpperInvariant() + ':' + $pidHex.ToUpperInvariant())
        if ($seen.Add($vp)) {
            $result.Add($vp)
        }
    }

    $lastVid = $null
    $parenDepth = 0
    # Scan left-to-right for explicit VID:PID, backticked 4-hex PID shorthands, and parentheses.
    $pattern = '([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})|`([0-9A-Fa-f]{4})`|([()])'
    $matches = [regex]::Matches($s, $pattern)
    foreach ($m in $matches) {
        if ($m.Groups[4].Success) {
            if ($m.Groups[4].Value -eq '(') { $parenDepth++ }
            elseif ($parenDepth -gt 0) { $parenDepth-- }
            continue
        }
        if ($m.Groups[1].Success) {
            $vid = $m.Groups[1].Value.ToUpperInvariant()
            $pidHex = $m.Groups[2].Value.ToUpperInvariant()
            $lastVid = $vid
            & $addVidPid $vid $pidHex
            continue
        }
        if ($m.Groups[3].Success -and $null -ne $lastVid) {
            $prefix = $s.Substring(0, $m.Index)
            $after = $s.Substring($m.Index + $m.Length)
            $afterConnector = $prefix -match '[/&,–\-]\s*$'
            # Require either parenthesized context (not immediately before "boards") or a list delimiter.
            if (($parenDepth -gt 0 -or $afterConnector) -and ($after -notmatch '^\s+boards\b')) {
                & $addVidPid $lastVid $m.Groups[3].Value
            }
        }
    }

    return [string[]]$result.ToArray()
}

$categoryByVendor = @{
    '8BitDo'                        = 'controllers'
    'AKG'                           = 'audio'
    'Akko'                          = 'mixed'
    'Alienware'                     = 'audio'
    'Anker Soundcore'               = 'audio'
    'Apple AirPods / Beats'         = 'audio'
    'Arbiter Studio'                = 'mice'
    'ASTRO'                         = 'audio'
    'ASUS ROG'                      = 'mixed'
    'ATK / VXE / VGN'               = 'mixed'
    'Attack Shark & Shared Receivers' = 'mixed'
    'Audeze'                        = 'audio'
    'AULA'                          = 'mixed'
    'BEITONG'                       = 'controllers'
    'BigBigWon'                     = 'controllers'
    'CHERRY / XTRFY'                = 'mixed'
    'Cooler Master'                 = 'mice'
    'Corsair'                       = 'mixed'
    'CROCIRIS / XIBERIA'            = 'mice'
    'DAREU'                         = 'mixed'
    'DARKRIM'                       = 'audio'
    'Darmoshark'                    = 'mice'
    'ENDORFY'                       = 'audio'
    'Endgame Gear'                  = 'mice'
    'EWEADN (前行者)'               = 'mice'
    'Fantech'                       = 'mice'
    'Finalmouse'                    = 'mice'
    'Flydigi'                       = 'controllers'
    'Fractal Design'                = 'audio'
    'G-Wolves'                      = 'mice'
    'GameSir'                       = 'controllers'
    'Glorious'                      = 'mice'
    'HECATE'                        = 'mice'
    'Hitscan'                       = 'mice'
    'HyperX'                        = 'mixed'
    'Incott'                        = 'mice'
    'Inphic'                        = 'mice'
    'IPI'                           = 'mice'
    'Jabra'                         = 'audio'
    'JBL'                           = 'audio'
    'Keychron'                      = 'mixed'
    'Kysona & Shared (3554/373B)'   = 'mice'
    'LAMZU'                         = 'mice'
    'Logitech'                      = 'mixed'
    'Machenike'                     = 'mice'
    'MAONO'                         = 'audio'
    'Marshall'                      = 'audio'
    'MCHOSE'                        = 'mixed'
    'MOONDROP'                      = 'audio'
    'Motospeed / Delux / Ajazz'     = 'mixed'
    'MSI'                           = 'mice'
    'Niceboy'                       = 'mice'
    'Ninjutso'                      = 'mice'
    'Nothing / CMF'                 = 'audio'
    'NuPhy'                         = 'keyboards'
    'NZXT'                          = 'mice'
    'Phones (Phone Link)'           = 'controllers'
    'PMO / WOBKEY / URX / EVO80'    = 'keyboards'
    'Poly / Plantronics'            = 'audio'
    'Press Play'                    = 'mice'
    'Pulsar'                        = 'mice'
    'Rapoo'                         = 'mice'
    'RAWM / SADES'                  = 'mice'
    'Razer'                         = 'mixed'
    'Redragon'                      = 'mixed'
    'ROCCAT'                        = 'audio'
    'Royal Kludge (RK)'             = 'mice'
    'Samsung'                       = 'audio'
    'Scyrox'                        = 'mice'
    'Sharkoon'                      = 'mice'
    'SONiX Keyboards'               = 'keyboards'
    'Sony'                          = 'mixed'
    'Sprime'                        = 'mice'
    'SteelSeries'                   = 'mixed'
    'Switch Pro-compatible'         = 'controllers'
    'Taidu (钛度)'                  = 'mice'
    'Turtle Beach'                  = 'audio'
    'Valve / Steam'                 = 'controllers'
    'VAXEE'                         = 'mice'
    'WLMouse'                       = 'mice'
    'Wraith'                        = 'mice'
    'Xbox'                          = 'controllers'
    'Zenith'                        = 'mice'
    'ZOWIE'                         = 'mice'
    'Universal Fallbacks'           = 'fallbacks'
}

function Resolve-Category([string]$vendor, [string]$models, [string]$protocol) {
    if ($categoryByVendor.ContainsKey($vendor)) {
        return $categoryByVendor[$vendor]
    }
    $text = ($vendor + ' ' + $models + ' ' + $protocol).ToLowerInvariant()
    if ($text -match 'fallback') { return 'fallbacks' }
    $hasMice = $text -match '\bmice\b|\bmouse\b'
    $hasKbd = $text -match '\bkeyboards?\b|\bboards\b'
    $hasAudio = $text -match '\bheadsets?\b|\bearbuds?\b|\bheadphones?\b|\bmicrophone\b'
    $hasCtrl = $text -match '\bcontrollers?\b|\bgamepad\b|\bphones?\b'
    $hits = @($hasMice, $hasKbd, $hasAudio, $hasCtrl | Where-Object { $_ }).Count
    if ($hits -gt 1) { return 'mixed' }
    if ($hasMice) { return 'mice' }
    if ($hasKbd) { return 'keyboards' }
    if ($hasAudio) { return 'audio' }
    if ($hasCtrl) { return 'controllers' }
    return 'mice'
}

function Escape-JsonString([string]$value) {
    $sb = [System.Text.StringBuilder]::new($value.Length + 8)
    [void]$sb.Append('"')
    foreach ($ch in $value.ToCharArray()) {
        $code = [int]$ch
        switch ($code) {
            34 { [void]$sb.Append('\"') }
            92 { [void]$sb.Append('\\') }
            8  { [void]$sb.Append('\b') }
            12 { [void]$sb.Append('\f') }
            10 { [void]$sb.Append('\n') }
            13 { [void]$sb.Append('\r') }
            9  { [void]$sb.Append('\t') }
            default {
                if ($code -lt 32) {
                    [void]$sb.Append(('\u{0:x4}' -f $code))
                } else {
                    [void]$sb.Append($ch)
                }
            }
        }
    }
    [void]$sb.Append('"')
    return $sb.ToString()
}

$markdown = ''
if (-not [string]::IsNullOrWhiteSpace($SourcePath)) {
    $markdown = [IO.File]::ReadAllText($SourcePath, [Text.UTF8Encoding]::new($false))
} else {
    $readmePath = Join-Path $root 'README.md'
    if (Test-Path -LiteralPath $readmePath) {
        $markdown = [IO.File]::ReadAllText($readmePath, [Text.UTF8Encoding]::new($false))
    }
    if ((Get-MatrixLines $markdown).Count -eq 0) {
        $markdown = Read-GitFileUtf8 $root 'main:README.md'
    }
}

$rows = Get-MatrixLines $markdown
if ($rows.Count -eq 0) {
    throw 'Could not locate the Hardware & Protocol Compatibility Matrix table.'
}

$entries = [System.Collections.Generic.List[object]]::new()
foreach ($row in $rows) {
    $trimmed = $row.Trim()
    if ($trimmed.StartsWith('|')) { $trimmed = $trimmed.Substring(1) }
    if ($trimmed.EndsWith('|')) { $trimmed = $trimmed.Substring(0, $trimmed.Length - 1) }
    $cols = $trimmed -split '\|'
    if ($cols.Count -lt 3) {
        throw "Unexpected table row format: $row"
    }

    $rawVendor = $cols[0].Trim()
    $rawModels = $cols[1].Trim()
    $rawProtocol = ($cols[2..($cols.Count - 1)] -join '|').Trim()

    $vendor = Clean-MarkdownCell $rawVendor
    $models = Clean-MarkdownCell $rawModels
    $protocol = Clean-MarkdownCell $rawProtocol
    $vidPids = Extract-VidPids $rawModels
    $tested = (($models + ' ' + $protocol) -imatch '\btested\b')
    $category = Resolve-Category $vendor $models $protocol

    $entries.Add([pscustomobject]@{
        vendor   = $vendor
        category = $category
        models   = $models
        protocol = $protocol
        vidPids  = $vidPids
        tested   = $tested
    })
}

$outSb = [System.Text.StringBuilder]::new()
[void]$outSb.AppendLine('[')
for ($i = 0; $i -lt $entries.Count; $i++) {
    $e = $entries[$i]
    [void]$outSb.AppendLine('  {')
    [void]$outSb.AppendLine('    "vendor": ' + (Escape-JsonString $e.vendor) + ',')
    [void]$outSb.AppendLine('    "category": ' + (Escape-JsonString $e.category) + ',')
    [void]$outSb.AppendLine('    "models": ' + (Escape-JsonString $e.models) + ',')
    [void]$outSb.AppendLine('    "protocol": ' + (Escape-JsonString $e.protocol) + ',')
    if ($e.vidPids.Count -eq 0) {
        [void]$outSb.AppendLine('    "vidPids": [],')
    } else {
        $vpJoined = ($e.vidPids | ForEach-Object { Escape-JsonString $_ }) -join ', '
        [void]$outSb.AppendLine('    "vidPids": [' + $vpJoined + '],')
    }
    $testedLiteral = if ($e.tested) { 'true' } else { 'false' }
    [void]$outSb.AppendLine('    "tested": ' + $testedLiteral)
    $comma = if ($i + 1 -lt $entries.Count) { ',' } else { '' }
    [void]$outSb.AppendLine('  }' + $comma)
}
[void]$outSb.Append(']')
[void]$outSb.Append("`n")

$outDir = Split-Path $OutputPath -Parent
if (-not [string]::IsNullOrWhiteSpace($outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}
[IO.File]::WriteAllText($OutputPath, $outSb.ToString(), [Text.UTF8Encoding]::new($false))
Write-Output ("Generated " + $entries.Count + " entries in " + $OutputPath)
