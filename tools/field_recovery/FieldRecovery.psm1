Set-StrictMode -Version 2
function Get-BytesDigest([byte[]]$Bytes) {
 $h=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($h.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}
}
function Sync-SnapshotFile([string]$Path) {
 $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Read);try{$stream.Flush($true)}finally{$stream.Dispose()}
}
function Save-DurableJson([string]$Path,$Value) {
 $bytes=(New-Object Text.UTF8Encoding($false)).GetBytes(($Value|ConvertTo-Json -Depth 8));$stream=New-Object IO.FileStream($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read,4096,[IO.FileOptions]::WriteThrough);try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function Get-SmartBoxKit([string]$KitDirectory) {
 $dir=(Resolve-Path -LiteralPath $KitDirectory).Path;$k=Get-Content -LiteralPath (Join-Path $dir 'kit.json') -Raw|ConvertFrom-Json
 $mp=Join-Path $dir 'manifest.json'
 if((Get-FileHash -LiteralPath $mp).Hash -ne $k.manifest_sha256){throw 'Manifest hash mismatch'}
 $m=Get-Content -LiteralPath $mp -Raw|ConvertFrom-Json
 if($m.schema -ne 1 -or $m.purpose -ne 'field_usb_recovery' -or -not $m.production_provisioned -or $m.chip -ne 'esp32c6' -or $m.build_environment -ne 'esp32-c6-field-recovery' -or -not $m.native_usb_serial -or $m.chip_id -ne 13 -or $m.flash_bytes -ne 16777216){throw 'Reviewed ESP32-C6/16MB recovery kit required'}
 if($m.version -notmatch '^0\.2\.1-g([a-f0-9]{7,40})$' -or $m.commit -notmatch '^[a-f0-9]{40}$' -or -not $m.commit.StartsWith(($m.version -split '-g')[1])){throw 'Clean matching source/version required'}
 $expected=@{app0=65536;app0_length=6553600;app1=6619136;app1_length=6553600;otadata=57344;otadata_length=8192;nvs=36864;nvs_length=20480;partition_table=32768}
 foreach($name in $expected.Keys){if($m.layout.$name -ne $expected[$name]){throw 'Unreviewed layout'}}
 $names=@($m.files.PSObject.Properties.Name|Sort-Object)
 if(($names -join ',') -ne 'boot_app0.bin,bootloader.bin,firmware.bin,partitions.bin'){throw 'Unexpected firmware files'}
 foreach($n in $names){$p=Join-Path $dir $n;if((Get-Item -LiteralPath $p).Length -ne $m.files.$n.size -or (Get-FileHash -LiteralPath $p).Hash -ne $m.files.$n.sha256){throw "Corrupt kit file: $n"}}
 $image=[IO.File]::ReadAllBytes((Join-Path $dir 'firmware.bin'))
 if($image.Length -le 256 -or $image.Length -gt 6553600 -or $image[0] -ne 233 -or ($image[3] -shr 4) -ne 4 -or [BitConverter]::ToUInt16($image,12) -ne 13 -or -not [Text.Encoding]::ASCII.GetString($image).Contains($m.version+[char]0)){throw 'Wrong application chip or version'}
 if($m.app_write_span -ne [int]([Math]::Ceiling($image.Length/4096.0)*4096) -or $m.files.'boot_app0.bin'.size -ne 8192){throw 'Invalid write span'}
 if($k.esptool_file -ne 'tools/esptool.exe'){throw 'Pinned esptool path required'}
 $tool=Join-Path $dir 'tools/esptool.exe';if((Get-FileHash -LiteralPath $tool).Hash -ne $k.esptool_sha256){throw 'Esptool hash mismatch'}
 $config=Join-Path $dir 'tools/esptool.cfg';if((Get-FileHash -LiteralPath $config).Hash -ne $k.esptool_config_sha256){throw 'Esptool config hash mismatch'}
 return @{directory=$dir;manifest=$m;tool=$tool;config=$config}
}
function Invoke-SmartBoxRecovery {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$KitDirectory,[string]$Port,[switch]$Apply,[switch]$MotorDisconnected,[switch]$UsbOnly,[string]$RestoreBackup,[ValidateSet(115200,460800)][int]$Baud=460800,[scriptblock]$Runner)
 $kit=Get-SmartBoxKit $KitDirectory;$m=$kit.manifest
 if(-not $Apply){return @{state='plan_only';version=$m.version;chip='esp32c6';flash_bytes=16777216;write_addresses=@('0xe000','0x10000');preserve=@('bootloader','partition_table','nvs','app1');device_contacted=$false}}
 if(-not $MotorDisconnected -or -not $UsbOnly -or $Port -notmatch '^COM[1-9][0-9]*$'){throw 'Apply requires explicit motor isolation, USB-only board power and COM port'}
 if(-not $Runner){$toolPath=$kit.tool;$configPath=$kit.config;$workingDirectory=$kit.directory;$Runner={param([string[]]$Arguments)
  $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$toolPath;$start.WorkingDirectory=$workingDirectory;$start.EnvironmentVariables['ESPTOOL_CFGFILE']=$configPath;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  $quoted=@($Arguments|ForEach-Object {if($_.Contains([string][char]34)){throw 'Invalid command argument'};([string][char]34)+$_+([string][char]34)});$start.Arguments=$quoted -join ' '
  $process=New-Object Diagnostics.Process;$process.StartInfo=$start
  try{[void]$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();if(-not $process.WaitForExit(600000)){$process.Kill();throw 'Esptool timeout; no retry'};$text=$stdout.Result+$stderr.Result;if($process.ExitCode -ne 0){throw "Esptool failed: $text"};return $text}finally{$process.Dispose()}
 }.GetNewClosure()}
 $common=@('--chip','esp32c6','--port',$Port,'--baud',"$Baud",'--after','no-reset')
 function Tool([string[]]$Command,[switch]$First){$reset=if($First){'default-reset'}else{'no-reset'};return (& $Runner -Arguments ($common+@('--before',$reset)+$Command))}
 $security=Tool @('get-security-info') -First
 if($security -notmatch '(?im)^Secure Boot:\s*Disabled\s*$' -or $security -notmatch '(?im)^Flash Encryption:\s*Disabled\s*$'){throw 'Secure/encrypted/unknown device: stop, never force'}
 $flash=Tool @('flash-id');if($flash -notmatch '(?im)Detected flash size:\s*16MB\s*$'){throw 'Actual16MB flash required;8MB is not compatible'}
 function DeviceMac { $line=Tool @('read-mac');if($line -notmatch '(?im)^(?:MAC|Base MAC):\s*([0-9a-f:]{17})\s*$'){throw 'Cannot identify MCU MAC'};return $Matches[1].ToLowerInvariant() }
 $mac=DeviceMac;$span=[int]$m.app_write_span;$app=Join-Path $kit.directory 'firmware.bin';$ota=Join-Path $kit.directory 'boot_app0.bin'
 if($RestoreBackup){
  $source=(Resolve-Path -LiteralPath $RestoreBackup).Path;$old=Get-Content -LiteralPath (Join-Path $source 'backup.json') -Raw|ConvertFrom-Json
  if($old.schema -ne 1 -or $old.mac -ne $mac -or $old.chip -ne 'esp32c6' -or $old.flash_bytes -ne 16777216 -or $old.span -lt 4096 -or $old.span -gt 6553600 -or $old.span%4096 -ne 0){throw 'Backup belongs to another device/layout'}
  foreach($n in @('boot-before.bin','app0-before.bin','otadata-before.bin')){$p=Join-Path $source $n;if((Get-FileHash -LiteralPath $p).Hash -ne $old.files.$n.sha256 -or (Get-Item -LiteralPath $p).Length -ne $old.files.$n.size){throw 'Backup is corrupt'}}
  $span=[int]$old.span;$app=Join-Path $source 'app0-before.bin';$ota=Join-Path $source 'otadata-before.bin'
  if((Get-Item $app).Length -ne $span -or (Get-Item $ota).Length -ne 8192){throw 'Incomplete restore regions'}
 }
 $backRoot=Join-Path $kit.directory 'backups';New-Item -ItemType Directory -Path $backRoot -Force|Out-Null
 $backup=Join-Path $backRoot ((Get-Date -Format yyyyMMdd-HHmmss)+'-'+[guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $backup|Out-Null
 $bootBefore=Join-Path $backup 'boot-before.bin';$appBefore=Join-Path $backup 'app0-before.bin';$otaBefore=Join-Path $backup 'otadata-before.bin'
 Tool @('read-flash','0x0','0x10000',$bootBefore)|Out-Null
 Tool @('read-flash','0x10000',('0x'+$span.ToString('x')),$appBefore)|Out-Null
 if((Get-Item $bootBefore).Length -ne 65536 -or (Get-Item $appBefore).Length -ne $span){throw 'Incomplete pre-write backup'}
 $before=[IO.File]::ReadAllBytes($bootBefore);$table=[IO.File]::ReadAllBytes((Join-Path $kit.directory 'partitions.bin'))
 if($table.Length -gt 4096 -or (Get-BytesDigest ([byte[]]$before[32768..(32768+$table.Length-1)])) -ne (Get-BytesDigest $table)){throw 'Actual partition table differs; no write'}
 [IO.File]::WriteAllBytes($otaBefore,[byte[]]$before[57344..65535])
 $saved=@{};foreach($n in @('boot-before.bin','app0-before.bin','otadata-before.bin')){$p=Join-Path $backup $n;$saved[$n]=@{sha256=(Get-FileHash $p).Hash.ToLowerInvariant();size=(Get-Item $p).Length}}
 foreach($p in @($bootBefore,$appBefore,$otaBefore)){Sync-SnapshotFile $p}
 $record=@{schema=1;mac=$mac;chip='esp32c6';flash_bytes=16777216;span=$span;kit_version_at_backup=$m.version;files=$saved;state='backed_up_before_single_cli_write'}
 Save-DurableJson (Join-Path $backup 'backup.json') $record
 if((DeviceMac) -ne $mac){throw 'Device changed after backup'}
 # These are the only write addresses. No erase-all, bootloader, table, NVS or app1 write.
 Tool @('write-flash','0xe000',$ota,'0x10000',$app)|Out-Null
 Tool @('verify-flash','0xe000',$ota,'0x10000',$app)|Out-Null
 $afterPath=Join-Path $backup 'boot-after.bin';Tool @('read-flash','0x0','0x10000',$afterPath)|Out-Null
 $after=[IO.File]::ReadAllBytes($afterPath)
 if($after.Length -ne 65536 -or (Get-BytesDigest ([byte[]]$before[0..57343])) -ne (Get-BytesDigest ([byte[]]$after[0..57343]))){throw 'Bootloader/table/NVS changed: keep download mode, inspect backup'}
 $result=@{state=$(if($RestoreBackup){'restored_verified'}else{'written_verified'});expected_version=$(if($RestoreBackup){$null}else{$m.version});restored_source=$(if($RestoreBackup){$source}else{$null});actual_boot_version_unconfirmed=$true;esptool_internal_retry_possible=$true;mac=$mac;nvs_unchanged=$true;left_in_download_mode=$true;backup_directory=$backup;physical_health_confirmed=$false}
 [IO.File]::WriteAllText((Join-Path $backup 'result.json'),($result|ConvertTo-Json));return $result
}
Export-ModuleMember -Function Get-SmartBoxKit,Invoke-SmartBoxRecovery
