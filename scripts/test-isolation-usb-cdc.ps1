param(
	[Parameter(Mandatory = $true)]
	[ValidatePattern('^COM\d+$')]
	[string]$PortName,
	[int]$BaudRate = 115200,
	[int]$TimeoutMilliseconds = 3000
)

$ErrorActionPreference = 'Stop'
$port = [System.IO.Ports.SerialPort]::new(
	$PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8,
	[System.IO.Ports.StopBits]::One)
$port.NewLine = "`n"
$port.ReadTimeout = 100
$port.WriteTimeout = $TimeoutMilliseconds

function Read-MatchingLine([string]$Pattern) {
	$watch = [System.Diagnostics.Stopwatch]::StartNew()
	while ($watch.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
		try {
			$line = $port.ReadLine().Trim()
			if ($line -match $Pattern) {
				Write-Host "RX: $line"
				return $line
			}
		} catch [System.TimeoutException] {
			# Keep waiting until the full response timeout expires.
		}
	}
	throw "Timed out waiting for '$Pattern' on $PortName."
}

function Send-AndExpect([string]$Command, [string]$Pattern) {
	Write-Host "TX: $Command"
	$port.WriteLine($Command)
	[void](Read-MatchingLine $Pattern)
}

try {
	$port.Open()
	[void]$port.ReadExisting()
	Start-Sleep -Milliseconds 150
	[void]$port.ReadExisting()

	Send-AndExpect 'STATUS' '^STATUS STOPPED generation=0$'
	Send-AndExpect 'PING before-start' '^ERR NOT_RUNNING; use START$'
	Send-AndExpect 'START' '^OK STARTED generation=1 user=1$'
	Send-AndExpect 'PING hello' '^ANSWER PING hello$'
	Send-AndExpect 'RESTART' '^OK RESTARTED generation=2 user=1$'
	Send-AndExpect 'PING after-restart' '^ANSWER PING after-restart$'
	Send-AndExpect 'CRASH' '^PLUGIN_FAULT reason=19 pc=0x[0-9a-fA-F]+ reap=0 abandon=0 unload=0; use RESTART$'
	Send-AndExpect 'STATUS' '^STATUS CRASHED generation=2$'
	Send-AndExpect 'RESTART' '^OK RESTARTED generation=3 user=1$'
	Send-AndExpect 'PING recovered' '^ANSWER PING recovered$'
	Send-AndExpect 'FINISH' '^OK FINISHED$'
	Send-AndExpect 'STATUS' '^STATUS STOPPED generation=3$'
	Send-AndExpect 'START' '^OK STARTED generation=4 user=1$'
	Send-AndExpect 'FINISH' '^OK FINISHED$'

	Write-Host 'PASS: CDC command/reply, graceful finish, crash containment, and restart cases.'
} finally {
	if ($port.IsOpen) {
		$port.Close()
	}
	$port.Dispose()
}
