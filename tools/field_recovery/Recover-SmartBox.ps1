param([string]$Port,[switch]$Apply,[switch]$MotorDisconnected,[switch]$UsbOnly,[string]$RestoreBackup,[ValidateSet(115200)][int]$Baud=115200)
$ErrorActionPreference='Stop'
try {
 Import-Module (Join-Path $PSScriptRoot 'FieldRecovery.psm1') -Force
 $result=Invoke-SmartBoxRecovery -KitDirectory $PSScriptRoot -Port $Port -Apply:$Apply -MotorDisconnected:$MotorDisconnected -UsbOnly:$UsbOnly -RestoreBackup $RestoreBackup -Baud $Baud
 $result|ConvertTo-Json -Depth 8
} catch {Write-Error $_;exit 1}
