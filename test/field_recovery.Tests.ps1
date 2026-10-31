param([string]$ModulePath=(Join-Path $PSScriptRoot '../tools/field_recovery/FieldRecovery.psm1'))
$ErrorActionPreference='Stop'
if (-not (Test-Path -LiteralPath $ModulePath)) { throw 'Field recovery module is missing (expected RED)' }
Import-Module $ModulePath -Force
function Assert($Condition,$Message){if(-not $Condition){throw $Message}}
$root=Join-Path ([IO.Path]::GetTempPath()) ('smartbox-test-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $root|Out-Null
$kit=Join-Path $root 'kit';New-Item -ItemType Directory $kit|Out-Null;New-Item -ItemType Directory (Join-Path $kit 'tools')|Out-Null
$version='0.2.1-gaaaaaaa';$image=New-Object byte[] 512;$image[0]=233;$image[3]=64;$image[12]=13;[Array]::Copy([Text.Encoding]::ASCII.GetBytes($version),0,$image,32,$version.Length)
[IO.File]::WriteAllBytes((Join-Path $kit 'firmware.bin'),$image);[IO.File]::WriteAllBytes((Join-Path $kit 'boot_app0.bin'),(New-Object byte[] 8192));[IO.File]::WriteAllBytes((Join-Path $kit 'bootloader.bin'),[byte[]](233,1,2));[IO.File]::WriteAllBytes((Join-Path $kit 'tools/esptool.exe'),[byte[]](1))
$stream=New-Object IO.MemoryStream;$writer=New-Object IO.BinaryWriter($stream)
foreach($r in @(@('nvs',1,2,0x9000,0x5000),@('otadata',1,0,0xe000,0x2000),@('app0',0,16,0x10000,0x640000),@('app1',0,17,0x650000,0x640000))){$writer.Write([uint16]0x50aa);$writer.Write([byte]$r[1]);$writer.Write([byte]$r[2]);$writer.Write([uint32]$r[3]);$writer.Write([uint32]$r[4]);$label=New-Object byte[] 16;[Array]::Copy([Text.Encoding]::ASCII.GetBytes($r[0]),$label,$r[0].Length);$writer.Write($label);$writer.Write([uint32]0)}
$table=New-Object byte[] 3072;[Array]::Copy($stream.ToArray(),$table,$stream.Length);[IO.File]::WriteAllBytes((Join-Path $kit 'partitions.bin'),$table)
$files=@{};foreach($n in @('firmware.bin','boot_app0.bin','bootloader.bin','partitions.bin')){$p=Join-Path $kit $n;$files[$n]=@{size=(Get-Item $p).Length;sha256=(Get-FileHash $p -Algorithm SHA256).Hash.ToLowerInvariant()}}
$manifest=@{schema=1;purpose='field_usb_recovery';production_provisioned=$true;build_environment='esp32-c6-field-recovery';native_usb_serial=$true;version=$version;commit=('a'*40);chip='esp32c6';chip_id=13;flash_bytes=16777216;app_write_span=4096;layout=@{app0=65536;app0_length=6553600;app1=6619136;app1_length=6553600;otadata=57344;otadata_length=8192;nvs=36864;nvs_length=20480;partition_table=32768};files=$files}
[IO.File]::WriteAllText((Join-Path $kit 'manifest.json'),($manifest|ConvertTo-Json -Depth 8))
[IO.File]::WriteAllText((Join-Path $kit 'tools/esptool.cfg'),('[esptool]'+[Environment]::NewLine))
$toolMeta=@{esptool_config_sha256=(Get-FileHash (Join-Path $kit 'tools/esptool.cfg')).Hash;esptool_file='tools/esptool.exe';esptool_sha256=(Get-FileHash (Join-Path $kit 'tools/esptool.exe')).Hash;manifest_sha256=(Get-FileHash (Join-Path $kit 'manifest.json')).Hash};[IO.File]::WriteAllText((Join-Path $kit 'kit.json'),($toolMeta|ConvertTo-Json))
function Context {
 $c=@{calls=(New-Object Collections.Generic.List[object]);flash='16MB';secure=$false;wrongChip=$false;badLayout=$false;failRead=$false;failVerify=$false;changeNvs=$false;mac='aa:bb:cc:dd:ee:ff';writes=0;root=$kit;table=$table}
 $runner={param([string[]]$Arguments)
  $c.calls.Add(@($Arguments));$a=@($Arguments);$cmd=$a|Where-Object {$_ -in @('get-security-info','flash-id','read-mac','read-flash','write-flash','verify-flash')}|Select-Object -First 1
  switch($cmd){
   'get-security-info' {if($c.wrongChip){throw 'wrong chip'};return ('Secure Boot: '+$(if($c.secure){'Enabled'}else{'Disabled'})+[Environment]::NewLine+'Flash Encryption: Disabled')}
   'flash-id' {return "Detected flash size: $($c.flash)"}
   'read-mac' {return "MAC: $($c.mac)"}
   'write-flash' {$c.writes++;return 'Hash of data verified.'}
   'verify-flash' {if($c.failVerify){throw 'verify failed'};return 'Verify successful.'}
   'read-flash' {
    if($c.failRead){throw 'read failed'}
    $idx=[Array]::IndexOf($a,'read-flash');$offset=[Convert]::ToInt32($a[$idx+1],16);$size=[Convert]::ToInt32($a[$idx+2],16);$file=$a[$idx+3];$data=New-Object byte[] $size
    if($offset -eq 0 -and $size -eq 65536){[Array]::Copy($c.table,0,$data,0x8000,$c.table.Length);if($c.badLayout){$data[0x8000]=0};if($c.changeNvs -and $c.writes){$data[0x9000]=77}}
    if($offset -eq 65536){$data[0]=233}
    [IO.File]::WriteAllBytes($file,$data);return 'Read complete.'
   }
   default {throw "Unexpected command $cmd"}
  }
 }.GetNewClosure()
 return @($c,$runner)
}
function Reject($Change){$pair=Context;$c=$pair[0];&$Change $c;$failed=$false;try{Invoke-SmartBoxRecovery -KitDirectory $kit -Port COM77 -Apply -MotorDisconnected -UsbOnly -Runner $pair[1]|Out-Null}catch{$failed=$true};Assert $failed 'unsafe recovery accepted';return $c}
$pair=Context;$dry=Invoke-SmartBoxRecovery -KitDirectory $kit -Port COM77 -Runner $pair[1];Assert ($pair[0].calls.Count -eq 0) 'default contacted device';Assert ($dry.state -eq 'plan_only') 'default plan missing'
foreach($change in @({param($c)$c.wrongChip=$true},{param($c)$c.flash='8MB'},{param($c)$c.secure=$true},{param($c)$c.badLayout=$true},{param($c)$c.failRead=$true})){$c=Reject $change;Assert ($c.writes -eq 0) 'unsafe write before completed backup'}
$c=Reject {param($c)$c.failVerify=$true};Assert ($c.writes -eq 1) 'verify failure retried'
$c=Reject {param($c)$c.changeNvs=$true};Assert ($c.writes -eq 1) 'NVS change not detected'
$pair=Context;$result=Invoke-SmartBoxRecovery -KitDirectory $kit -Port COM77 -Apply -MotorDisconnected -UsbOnly -Runner $pair[1];Assert ($result.nvs_unchanged -and $result.left_in_download_mode) 'normal recovery did not preserve state'
$write=$pair[0].calls|Where-Object {$_ -contains 'write-flash'};Assert (($write -join ' ') -notmatch 'erase|0x0 ') 'factory erase/bootloader write';Assert (($write -join ' ') -match '0xe000 .*0x10000 ') 'wrong write offsets'
$pair=Context;$restored=Invoke-SmartBoxRecovery -KitDirectory $kit -Port COM77 -Apply -MotorDisconnected -UsbOnly -RestoreBackup $result.backup_directory -Runner $pair[1];Assert $restored.nvs_unchanged 'restore changed NVS'
$pair=Context;$pair[0].mac='11:22:33:44:55:66';$failed=$false;try{Invoke-SmartBoxRecovery -KitDirectory $kit -Port COM77 -Apply -MotorDisconnected -UsbOnly -RestoreBackup $result.backup_directory -Runner $pair[1]|Out-Null}catch{$failed=$true};Assert ($failed -and $pair[0].writes -eq 0) 'restore accepted other device'
$failed=$false;try{Invoke-SmartBoxRecovery -KitDirectory $kit -Port COM77 -Apply|Out-Null}catch{$failed=$true};Assert $failed 'missing physical prerequisites accepted'
$bytes=[IO.File]::ReadAllBytes((Join-Path $kit 'firmware.bin'));$bytes[40]=1;[IO.File]::WriteAllBytes((Join-Path $kit 'firmware.bin'),$bytes);$failed=$false;try{Get-SmartBoxKit $kit|Out-Null}catch{$failed=$true};Assert $failed 'corrupt kit accepted'
Write-Output 'FIELD_RECOVERY_TESTS_PASS=13'
