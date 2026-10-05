param(
    [string]$Port = "COM7",
    [int]$BaudRate = 115200,
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 250
$serial.WriteTimeout = 1000

function Read-Response {
    param([string]$Pattern, [string]$Description)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $text = ""
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($serial.BytesToRead -gt 0) {
            $text += $serial.ReadExisting()
            if ($text -match $Pattern) { return $text }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
    throw "Timed out waiting for $Description. Received: $text"
}

function Invoke-Command {
    param([string]$Command, [string]$Pattern, [string]$Description)
    $serial.WriteLine($Command)
    return Read-Response -Pattern $Pattern -Description $Description
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("")
    Read-Response -Pattern "PluginManager host ready|pluginmanager|uart:~\$" -Description "host startup" | Out-Null

    $status = Invoke-Command "plugin status" "mounted=(yes|no).*storage_validated=(yes|no)" "plugin status"
    if ($status -match "mounted=no") {
        Write-Output "BLOCKED: external storage is not mounted"
        Write-Output $status
        exit 2
    }
    $check = Invoke-Command "plugin storage-check" "storage packages valid|storage package validation failed|storage unavailable|Unknown command|uart:~\$" "storage validation"

    if ($check -match "storage unavailable|storage package validation failed|Unknown command") {
        Write-Output "BLOCKED: external storage is not mounted or package validation failed"
        Write-Output $status
        Write-Output $check
        exit 2
    }
    if ($status -notmatch "mounted=yes.*storage_validated=yes") {
        throw "Storage validation passed but status was unexpected: $status"
    }
    Write-Output $status
    Write-Output $check
    Write-Output "PASS: both packages were validated from mounted storage with the streaming reader"
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
