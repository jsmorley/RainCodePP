function Get-NotepadProfile([string]$Directory) {
    if (Test-Path -LiteralPath (Join-Path $Directory 'doLocalConf.xml')) { return $Directory }
    Join-Path $env:APPDATA 'Notepad++'
}

function Get-ApplicationBackup([string]$Directory) {
    Join-Path $Directory 'plugins\RainCodePP-backup\Original RainLexer'
}

function Get-ProfileBackup([string]$Profile) {
    Join-Path $Profile 'plugins\config\RainCodePP-backup\Original RainLexer'
}

function Get-OrdinaryFiles($Item) {
    if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
    if ($Item.PSIsContainer) {
        foreach ($child in Get-ChildItem -LiteralPath $Item.FullName -Force) {
            Get-OrdinaryFiles $child
        }
    } else { $Item }
}

function Copy-ClassicFiles([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { return }
    foreach ($relative in @('plugins','plugins\config')) {
        $path = Join-Path $Source $relative
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
        $parent = Get-Item -LiteralPath $path -Force
        if ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        foreach ($entry in Get-ChildItem -LiteralPath $path -Force) {
            if ($entry.Name -notmatch '^RainLexer(?:$|[ ._-]|Vocabulary|Styles|Keywords)') { continue }
            foreach ($file in Get-OrdinaryFiles $entry) {
                $subpath = $file.FullName.Substring($Source.TrimEnd('\').Length).TrimStart('\')
                $target = Join-Path $Destination $subpath
                if (Test-Path -LiteralPath $target) { continue }
                $folder = Split-Path -Parent $target
                # Avoid following a redirected directory when restoring files.
                $cursor = $Destination
                $segments = (Split-Path -Parent $subpath) -split '\\'
                $redirected = $false
                foreach ($segment in $segments) {
                    $cursor = Join-Path $cursor $segment
                    if ((Test-Path -LiteralPath $cursor) -and
                        ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                        $redirected = $true
                        break
                    }
                }
                if ($redirected) { continue }
                New-Item -ItemType Directory -Path $folder -Force | Out-Null
                [IO.File]::Copy($file.FullName, $target)
            }
        }
    }
}

function Write-ClassicBackup([string]$Source, [string]$Backup) {
    if (Test-Path -LiteralPath $backup) { return }
    $staging = "$backup-$([guid]::NewGuid().ToString('N')).tmp"
    Copy-ClassicFiles $Source $staging
    if (Test-Path -LiteralPath $staging) {
        [IO.Directory]::Move($staging, $backup)
        Write-Output "Original RainLexer backed up to $backup"
    }
}

function Backup-ClassicRainLexer([string]$Directory, [string]$Profile) {
    # Back up program files beside Notepad++ so writable profile data cannot be
    # restored into Program Files by an elevated uninstaller.
    Write-ClassicBackup $Directory (Get-ApplicationBackup $Directory)
    if ($Profile -ne $Directory) {
        Write-ClassicBackup $Profile (Get-ProfileBackup $Profile)
    }
}

function Restore-MissingClassicRainLexer([string]$Directory, [string]$Profile) {
    Copy-ClassicFiles (Get-ApplicationBackup $Directory) $Directory
    if ($Profile -ne $Directory) {
        Copy-ClassicFiles (Get-ProfileBackup $Profile) $Profile
    }
}

