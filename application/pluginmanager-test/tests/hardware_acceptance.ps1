param(
    [string]$Port = "COM7",
    [int]$BaudRate = 115200,
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"

function Read-Until {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Pattern,
        [string]$Description
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $text = ""
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Serial.BytesToRead -gt 0) {
            $text += $Serial.ReadExisting()
            if ($text -match $Pattern) {
                return $text
            }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
    throw "Timed out waiting for $Description. Received: $text"
}

function Invoke-ShellCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [string]$Pattern,
        [string]$Description
    )

    $Serial.WriteLine($Command)
    return Read-Until -Serial $Serial -Pattern $Pattern -Description $Description
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 250
$serial.WriteTimeout = 1000

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("")
    Read-Until -Serial $serial -Pattern "PluginManager host ready|pluginmanager|uart:~\$" -Description "host startup" | Out-Null

    Invoke-ShellCommand $serial "plugin status" "mounted=(yes|no).*loaded=no.*instance=no" "initial plugin status" | Out-Null
    $list = Invoke-ShellCommand $serial "plugin list" "(ELTA3|FTS3XX|No plugins found)" "plugin discovery"
    if ($list -notmatch "ELTA3" -or $list -notmatch "FTS3XX") {
        throw "Both root packages were not discovered: $list"
    }

    $plugin2Assignment = Invoke-ShellCommand $serial "plugin assign 2 2" "plugin 2 assigned port 2" "plugin 2 assignment"
    if ($plugin2Assignment -notmatch "status 0") {
        throw "Plugin 2 assignment failed: $plugin2Assignment"
    }

    $plugin2 = "2"
    $selection = Invoke-ShellCommand $serial "plugin select $plugin2" "plugin $plugin2 selected|select failed" "plugin $plugin2 selection"
    if ($selection -notmatch "plugin $plugin2 selected") {
        throw "Plugin $plugin2 was not selected: $selection"
    }
    Invoke-ShellCommand $serial "plugin status" "loaded=yes.*instance=yes.*selected=$plugin2" "plugin $plugin2 loaded status" | Out-Null
    Invoke-ShellCommand $serial "plugin unload" "plugin unloaded" "plugin $plugin2 unload" | Out-Null

    $disablePlugin2 = Invoke-ShellCommand $serial "plugin assign 2 0" "plugin 2 assigned port 0" "plugin 2 disable"
    if ($disablePlugin2 -notmatch "status 0") {
        throw "Plugin 2 disable failed: $disablePlugin2"
    }
    $plugin3Assignment = Invoke-ShellCommand $serial "plugin assign 3 2" "plugin 3 assigned port 2" "plugin 3 assignment"
    if ($plugin3Assignment -notmatch "status 0") {
        throw "Plugin 3 assignment failed: $plugin3Assignment"
    }

    $plugin = "3"
        $selection = Invoke-ShellCommand $serial "plugin select $plugin" "plugin $plugin selected|select failed" "plugin $plugin selection"
        if ($selection -notmatch "plugin $plugin selected") {
            throw "Plugin $plugin was not selected: $selection"
        }

        Invoke-ShellCommand $serial "plugin status" "loaded=yes.*instance=yes.*selected=$plugin" "plugin $plugin loaded status" | Out-Null
        Invoke-ShellCommand $serial "plugin unload" "plugin unloaded" "plugin $plugin unload" | Out-Null
        Invoke-ShellCommand $serial "plugin status" "loaded=no.*instance=no.*selected=0" "plugin $plugin final status" | Out-Null

    Invoke-ShellCommand $serial "plugin assign 3 0" "plugin 3 assigned port 0" "plugin 3 disable" | Out-Null
    Invoke-ShellCommand $serial "plugin assign 2 2" "plugin 2 assigned port 2" "restore plugin 2 assignment" | Out-Null

    Write-Output "PASS: both plugins selected, instantiated, status-checked, and unloaded"
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
