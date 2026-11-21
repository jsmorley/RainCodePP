function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-ApplicationWritable([string]$Directory) {
    # Try each existing parent that may receive files. A portable copy in a
    # user-writable directory needs no elevation.
    foreach ($relative in @('plugins','plugins\RainSyntax','plugins\RainSyntax\Presets',
            'plugins\RainSyntax-backup','plugins\RainCodePP-backup','plugins\Config')) {
        $parent = Join-Path $Directory $relative
        while (!(Test-Path -LiteralPath $parent -PathType Container)) { $parent = Split-Path -Parent $parent }
        $probe = Join-Path $parent ("RainSyntax-" + [guid]::NewGuid().ToString('N') + '.tmp')
        try {
            $stream = [IO.FileStream]::new($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
                [IO.FileShare]::None, 4096, [IO.FileOptions]::DeleteOnClose)
            $stream.Dispose()
        } catch [UnauthorizedAccessException] { return $false }
    }
    return $true
}

function Invoke-ElevatedInstaller([string]$Executable, [string]$Arguments) {
    try {
        # Elevate the original installer, which extracts its own payload.
        # The elevated process never executes scripts from the caller's temp folder.
        $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
    } catch [System.ComponentModel.Win32Exception] {
        if ($_.Exception.NativeErrorCode -eq 1223) { throw 'Administrator approval was canceled.' }
        throw
    }
    if ($process.ExitCode -ne 0) { throw "The administrator step failed (exit code $($process.ExitCode))." }
}
