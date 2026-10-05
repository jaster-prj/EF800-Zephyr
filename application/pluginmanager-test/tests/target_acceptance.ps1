param(
    [string]$Port = "COM14",
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 250
$serial.WriteTimeout = 1000

function Read-Until {
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
    return Read-Until -Pattern $Pattern -Description $Description
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("")
    Read-Until -Pattern "uart:~\$|ef800 main application" -Description "MainApplication startup" | Out-Null

    $status = Invoke-Command "plugin status" "|Plugin|plugin|Unknown command" "plugin status"
    if ($status -match "mounted=no") {
        Write-Output "BLOCKED: internal storage is not reachable"
        exit 2
    }

    $list = Invoke-Command "plugin list" "|plugin|Unknown command" "plugin listing"
    $calc = Invoke-Command "calc status" "|calc|calculation|Unknown command" "calculation status"

    Write-Output $status
    Write-Output $list
    Write-Output $calc
    Write-Output "PASS: startup discovery, plugin registry, and calculation diagnostics reachable"
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
