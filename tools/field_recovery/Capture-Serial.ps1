param([Parameter(Mandatory=$true)][ValidatePattern('^COM[1-9][0-9]*$')][string]$Port,[ValidateRange(10,300)][int]$Seconds=90)
$ErrorActionPreference='Stop'
# Explicit field monitor action; DTR/RTS stay off and no command bytes are sent.
$dir=Join-Path $PSScriptRoot 'logs';New-Item -ItemType Directory $dir -Force|Out-Null
$path=Join-Path $dir ('serial-'+(Get-Date -Format yyyyMMdd-HHmmss)+'.txt')
$serial=New-Object IO.Ports.SerialPort $Port,115200,None,8,one
$serial.DtrEnable=$false;$serial.RtsEnable=$false;$serial.ReadTimeout=500
$writer=New-Object IO.StreamWriter($path,$false,(New-Object Text.UTF8Encoding($false)))
try {$serial.Open();$end=[DateTime]::UtcNow.AddSeconds($Seconds);while([DateTime]::UtcNow -lt $end){$text=$serial.ReadExisting();if($text){$writer.Write($text);$writer.Flush();Write-Host -NoNewline $text};Start-Sleep -Milliseconds 100}}finally{if($serial.IsOpen){$serial.Close()};$serial.Dispose();$writer.Dispose()}
Write-Output "Saved private serial log: $path"
