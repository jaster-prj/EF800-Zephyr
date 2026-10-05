param(
    [string]$ConsolePort = "COM7",
    [int]$BaudRate = 115200,
    [ValidateSet("elta3", "fennel1")]
    [string]$Instrument = "elta3",
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($ConsolePort, $BaudRate,
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

$plugin = if ($Instrument -eq "elta3") { "2" } else { "3" }

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("")
    Read-Until -Pattern "PluginManager host ready|pluginmanager|uart:~\$" -Description "host startup" | Out-Null
    Invoke-Command "plugin assign 2 0" "plugin 2 assigned port 0.*status 0" "disable ELTA3 binding" | Out-Null
    Invoke-Command "plugin assign 3 0" "plugin 3 assigned port 0.*status 0" "disable FTS3XX binding" | Out-Null
    Invoke-Command "plugin assign $plugin 2" "plugin $plugin assigned port 2.*status 0" "assign device binding" | Out-Null
    Invoke-Command "plugin select $plugin" "plugin $plugin selected" "plugin selection" | Out-Null
    $result = Invoke-Command "kb measure" "distance_mm=-?[0-9]+" "measurement result"
    if ($result -notmatch "distance_mm=-?[0-9]+.*horizontal_gon_1e4=-?[0-9]+.*vertical_gon_1e4=-?[0-9]+") {
        throw "Malformed measurement result: $result"
    }
    Invoke-Command "plugin unload" "plugin unloaded" "plugin unload" | Out-Null
    Write-Output "PASS: $Instrument dummy device produced a structured measurement through plugin $plugin"
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
