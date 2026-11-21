function Get-NotepadApps([string]$Exe, [string]$SettingsApp) {
    Get-Process 'notepad++','RainCodePPSettings' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -in @($Exe,$SettingsApp) }
}

function Wait-ForNotepadAppsToClose($Apps) {
    # Use each application's normal close flow, including its save prompts.
    foreach ($app in $Apps) { [void]$app.CloseMainWindow() }
    $deadline = [datetime]::UtcNow.AddSeconds(45)
    foreach ($app in $Apps) {
        $remaining = [Math]::Max(0, [int]($deadline - [datetime]::UtcNow).TotalMilliseconds)
        if (-not $app.WaitForExit($remaining)) { return $false }
    }
    return $true
}
