# SmartBox 현장 USB 복구 안내

작성: 2026-11-01 KST. 대상은 SmartBox 자동 수거함 ESP32-C6이며 출입문 Target용 파일이 아닙니다.
현재 설치본 0.2.0-g7a55dd8의 전압 측정은 무효 상태입니다. 현장용 수정본의 정확한 버전·커밋·해시는 함께 받은 manifest.json과 BUILD-RECEIPT.json을 기준으로 합니다. 준비·빌드 성공과 실제 현장 복구는 별도입니다.

## 방문 전에 챙길 것

| 구분 | 준비물 / 규격 | 용도 |
|---|---|---|
| 필수 | Windows 10/11 x64 노트북, 충전기, 충분한 배터리 | 오프라인 USB 복구·로그 저장 |
| 필수 | 데이터 전송 가능한 USB-C 케이블 2개, 노트북에 맞는 USB 어댑터 | 충전 전용 케이블 제외; 짧은 케이블 권장 |
| 필수 | DC 전압을 측정할 멀티미터와 절연 프로브 | 배터리/Vin+/5V/3.3V/접지 확인 |
| 필수 | 절연 드라이버, 라벨·테이프, 휴대폰 카메라, 뚜껑 고정 받침 | 기존 배선 기록, 모터 전원선 분리·절연 |
| 권장 | INA219 모듈 1개: I2C 0x40, 0.1옴 R100 션트, 3.3V 로직 호환 | 센서·단자 불량 시 교체. INA226/INA3221/R010으로 대체하지 않기 |
| 권장 | 동일 규격 점퍼선·단자·기존 정격의 예비 퓨즈 | 단선·접촉 불량 대응 |
| 선택 | 배터리 라벨의 종류·전압과 맞는 충전기 | 납산/LiFePO4 등 혼용하지 않기; 종류 확인 전 연결하지 않기 |
| 선택 | 동일 핀맵의 예비 ESP32-C6 보드 | 실측 16MB 확인 제품만. 흔한 공식 N8은 8MB라 본 키트와 다름; 교체 보드 초기 설치는 이 자동 도구 범위 밖 |

부품을 구매하거나 드라이버를 설치하지는 않았습니다. 실제 보드·케이블·COM 인식은 방문 전/현장에서 확인해야 합니다.

## 폴더와 사전 실행

키트에는 운영용 인증정보가 포함된 펌웨어와 현장에서 만들 설정 백업이 들어갑니다. 공개 저장소·단체 채팅에 올리지 말고 본인 전용 폴더에 보관합니다. 개인키·실제 비밀번호를 채팅에 보낼 필요는 없습니다.

- Recover-SmartBox.ps1: 기본은 오프라인 해시 확인·계획 출력. Apply에서만 장치 접속.
- FieldRecovery.psm1: 칩/보안/플래시/파티션/백업/쓰기 검증.
- tools/esptool.exe: 공식 esptool 5.3.0 휴대용 도구. Python 설치 불필요.
- tools/esptool.cfg: 검증된 고정 설정. 사용자 PC의 커스텀 리셋 설정을 상속하지 않음.
- Capture-Serial.ps1: 시리얼 로그 저장. 명령 바이트를 전송하지 않음.
- firmware.bin: 현장용 네이티브 USB CDC 로그가 켜진 수정 앱.
- boot_app0.bin: app0를 선택할 OTA 선택 정보 초기값.
- partitions.bin / bootloader.bin: 레이아웃·복구 참고 자료. 기본 도구는 쓰지 않음.
- manifest.json / kit.json / BUILD-RECEIPT.json: 출처·버전·파일 무결성.
- backups / logs: 현장에서 자동 생성. 이 파일들도 비공개로 보관.

키트 폴더에서 PowerShell을 열고 아래 명령을 실행합니다. 이 단계는 USB를 꽂지 않아도 되고 장치 리셋/쓰기를 하지 않습니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Recover-SmartBox.ps1

정상 출력: state=plan_only, device_contacted=false, 칩 esp32c6, flash_bytes=16777216, 쓰기 주소 0xe000/0x10000. ExecutionPolicy Bypass는 이 PowerShell 프로세스에만 적용되며 전역 정책을 바꾸지 않습니다.

## 1. 전원·기구를 먼저 분리

1. 뚜껑을 받침으로 고정하고 사람·손이 액추에이터 동작 범위에 없도록 합니다. 기존 배선과 보드 모델·단자 라벨을 사진으로 남깁니다.
2. 장치 메인 토글을 OFF로 합니다. 태양광 충전기 배터리/PV 배선은 USB 복구를 위해 해체하지 않습니다.
3. 모터로 가는 12V 부하 전원 연결을 물리적으로 분리하고 각각 절연합니다. 릴레이 제어 GPIO 선만 빼는 것은 모터 전원 분리가 아닙니다. 어떤 선인지 식별이 안 되면 진행하지 않습니다.
4. LM2596에서 보드 5V 헤더로 들어오는 외부 5V 연결을 분리합니다. 복구 중 보드 전원은 USB 한 가지로만 공급합니다. USB 전원을 켠 채 외부 5V/3V3 전원을 함께 연결하지 않습니다.
5. 센서 VCC와 I2C pull-up은 ESP32-C6의 3.3V 로직 기준으로 확인합니다. SDA=22/SCL=23, INA219 VCC와 고전압 Vin+는 다른 단자입니다. 12V를 VCC/GPIO에 연결하지 않습니다. 5V pull-up이 직접 GPIO로 들어오면 먼저 전원을 끄고 3.3V 호환 배선/레벨 변환을 확인합니다.

기존 field 배선의 레벨 변환 유무는 원격으로 검증되지 않았습니다. 이 확인은 펌웨어 원인과 별개이며, 5V 배선을 현재 장애 원인으로 단정하는 것이 아닙니다.

## 2. USB 다운로드 모드와 COM 확인

1. 현장용 이미지의 기본 포트는 보드의 ESP32-C6 USB / USB Serial-JTAG 표시 포트입니다. USB-to-UART 표시 포트와 구분합니다.
2. BOOT 버튼을 누른 상태에서 RESET/EN을 한 번 누르고, 다운로드 포트가 나타나면 BOOT를 놓습니다. 다른 GPIO를 임의로 쇼트하지 않습니다.
3. Windows 장치 관리자 또는 아래 명령으로 새 COM 번호를 확인합니다. COM7은 예시입니다.

    [IO.Ports.SerialPort]::GetPortNames()

네이티브 USB는 일반적으로 Windows의 CDC 직렬 드라이버로 인식합니다. 이 PC에서는 실제 보드가 없어 인식 시험은 아직 하지 못했습니다. COM이 없으면 다른 데이터 케이블·USB 포트로 확인하고 보드 포트 표시를 재확인합니다. UART 브리지가 CP210x인 경우 [Silicon Labs 공식 드라이버](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers)를 방문 전에 준비합니다. 드라이버 자동 설치·PC 전역 설정 변경은 하지 않았으며 CP210x ZIP은 다운로드 실패로 포함되지 않았습니다.

## 3. 현재 보드에 수정 앱 기록

물리적 분리 확인을 마친 뒤에만 실행합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Recover-SmartBox.ps1 -Port COM7 -Apply -MotorDisconnected -UsbOnly

도구가 순서대로 수행하는 작업:

1. Secure Boot/Flash Encryption이 꺼진 ESP32-C6인지 확인. 알 수 없거나 켜져 있으면 강제 옵션 없이 중단.
2. 실제 플래시가 16MB인지 확인. 8MB·다른 크기이면 쓰지 않고 중단. 물리적 플래시 용량은 아직 미확인입니다.
3. MCU MAC을 기록하고 기존 파티션이 키트와 정확히 같은지 대조.
4. 첫 64KiB(부트로더·파티션·NVS·OTA 선택 정보) 및 새 앱이 덮어쓸 app0 섹터만 보존. 파일과 메타데이터를 디스크에 동기화한 뒤 진행. 전체 NAS 백업 작업은 재개하지 않음.
5. 같은 MAC을 다시 확인하고 0xe000의 OTA 선택 정보, 0x10000의 app0만 기록. 전체 erase, NVS·부트로더·파티션·app1 쓰기는 하지 않음.
6. 명시적 flash 검증과 NVS/부트로더/파티션 불변 확인. 성공하면 state=written_verified, nvs_unchanged=true, left_in_download_mode=true.

공식 esptool은 통신 오류 때 제한된 내부 재접속/재쓰기를 할 수 있습니다. 스크립트는 쓰기 작업을 자동 반복하지 않지만 실제 전송 횟수를 1회로 보장하지는 않습니다. 작업이 끝나기 전 USB·대상 보드를 빼거나 바꾸지 않습니다. 오류 시에는 다운로드 모드라고 가정하지 말고 모터를 분리한 상태로 중단합니다. 느린 USB-UART 연결은 명시적으로 -Baud 115200으로 다시 검토할 수 있으나, 실패 상태·백업을 먼저 확인하고 무작정 반복하지 않습니다.

8MB 또는 알 수 없는 파티션이면 16MB를 강제 지정하거나 파티션을 현장에서 축소하지 않습니다. 보드/플래시 ID와 출력·백업을 보존하여 별도 호환 복구안을 판단합니다. 빈 교체 보드·부트로더/파티션 손상은 이 앱 전용 자동 복구 절차로 처리하지 않습니다.

## 4. 모터를 분리한 채 측정 확인

1. BOOT가 놓인 상태에서 RESET/EN을 한 번 눌러 새 앱을 시작합니다. USB COM 번호가 바뀔 수 있으니 다시 확인합니다. 전압 확인 전 모터를 연결하지 않습니다.
2. 네이티브 USB 포트에서 아래 명령으로 로그를 90초 저장합니다. 시작 줄까지 보려면 수집을 연 뒤 RESET을 한 번 누릅니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Capture-Serial.ps1 -Port COM7 -Seconds 90

3. 설치 버전이 manifest.json의 version과 같은지 확인합니다. 앱은 [SYSTEM] Firmware와 매 5초 [POWER-DIAG]를 출력합니다. 현장용 USB 로그는 host가 없어도 기다리지 않도록 TX 대기 시간을 0으로 설정했으며, 실제 분리/재접속 시험은 현장 Gate입니다.
4. 배터리 전압을 보려면 INA219 Vin+가 배터리에 연결되어 있어야 합니다. 모터 12V 부하는 계속 분리하고 보드 외부 5V도 계속 분리한 채, 확인한 토글 경로로 Vin+만 실제 배터리 전압이 걸리는 조건을 만듭니다. 접지는 공통이어야 합니다. USB만 켜고 Vin+를 끈 상태의 0V/측정 무효는 정상 전원 측정 시험이 아닙니다.
5. 멀티미터는 DC V 모드, 검은 리드는 COM, 빨간 리드는 V 단자에 둡니다. 배터리/INA219 Vin+와 GND, 보드 3.3V, LM2596 출력 5.0V를 확인합니다. 전류 단자/A 모드로 배터리 양단을 연결하지 않습니다.

정상 진단 예: driver=1, fault=0, config=0x399f, calibration=4096, valid=1, age_ms가 2000 이하. 전압이 실제 배터리/DMM과 설명 가능한 수준으로 일치하는지 확인하고 기록합니다. 부하가 분리된 상태의 0mA 부근 전류는 정상일 수 있습니다. 이 센서는 모터 경로를 측정하므로 MCU·공유기까지 포함한 전체 소비전력 측정으로 해석하지 않습니다.

| fault | 의미 | 다음 확인 |
|---|---|---|
| 1/3/5/6/7/10 | 초기화·I2C 읽기 실패 | VCC/GND, SDA22/SCL23, 단자 접촉, 0x40 모듈 여부, pull-up 로직 전압 |
| 2 | 설정 프로파일 불일치 | INA219와 호환 모듈인지 확인, config 값·모듈 사진 기록 |
| 4 | calibration 불일치 | 모듈 종류·복귀 재초기화 상태 확인, calibration 값 기록 |
| 8/9 | 유한성·범위 오류 | Vin+ 미인가/0V/역결선/전압 범위 확인; DMM과 대조 |
| 0인데 valid=0 | 최근 측정/상태 정책도 확인 필요 | 배터리 실제 전압, age_ms와 상태·로그 함께 기록 |

여전히 센서가 무효이면 펌웨어를 반복 기록하거나 전압 보호 한계를 낮추지 않습니다. USB와 토글을 모두 끈 후 같은 규격의 예비 INA219로 교체하고 연결을 재점검할 수 있습니다. 원본 센서·배선 사진·로그를 보존합니다.

## 5. 정상 전원·동작 복귀와 완료 판정

1. USB를 먼저 분리하고 토글 OFF 상태에서 외부 5V 배선을 원래대로 복구합니다. 다시 측정한 LM2596 출력이 5.0V인 것을 확인하고 보드에 연결합니다. 두 전원을 동시에 연결하지 않습니다.
2. 모터 전원은 아직 분리한 상태에서 정상 배터리 전원으로 부팅하고 MQTT/Grafana의 실제 수신 시각·버전·sensor_valid를 확인합니다. 예전 12.12V/retained 값만으로 현재 측정 성공을 판정하지 않습니다.
3. 전압이 유효해도 회복 기준 미만이면 보호 정지가 맞습니다. 기본 회복은 12.4V 이상 300초 유지이며, 저장된 cutoff가 더 높으면 cutoff+0.8V를 따릅니다. 유효 12.17V에서 보호 상태라고 설치 실패로 판정하지 않습니다. 실제 배터리 종류에 맞게 충전하거나 태양광 충전을 기다립니다.
4. 지속 측정·충전 회복을 확인한 후, 토글 OFF에서 모터 전원 배선을 복구합니다. 사람이 현장에 있는 상태에서 주변을 비우고 정상 개폐 1회를 감독합니다. 원격 무인 개폐는 하지 않습니다.
5. 다음 기록을 남깁니다: 보드 모델/실측 flash ID/MAC, 설치 버전, DMM 배터리·Vin+·5V·3.3V, fault/config/calibration/valid, 최근 수신 시각, 충전 회복 300초 결과, 실제 개폐 결과. GPIO·극성·전압 기준은 임의 변경하지 않습니다.
6. 현장 건강 확인 후에만 PR의 실제 적용 Gate와 이슈19를 갱신하고 운영 manifest 게시 보류 해제/향후 OTA를 별도로 검증합니다. USB 쓰기·verify만으로 OTA 건강/물리 복구 완료를 선언하지 않습니다.

## 되돌리기

새 앱이 부팅하지 않거나 진단이 악화되면 모터 전원을 분리한 채 다시 ROM 다운로드 모드로 들어갑니다. 같은 장치의 backups 하위 폴더를 지정합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Recover-SmartBox.ps1 -Port COM7 -Apply -MotorDisconnected -UsbOnly -RestoreBackup '.\backups\해당-백업폴더'

도구는 같은 MAC·플래시·백업 해시를 확인한 뒤 원래 덮어쓴 app0 범위와 원래 otadata만 되돌립니다. NVS와 다른 슬롯은 유지합니다. 복원된 실제 버전은 로그로 다시 확인해야 하며, 이전 앱의 센서 장애까지 해결된다고 보장하지 않습니다. 백업을 다른 보드에 강제 적용하지 않습니다.

## 공식 참고

- [Espressif 보드 설명: 두 USB 포트, BOOT/RESET, 단일 전원 방식과 N8 용량](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)
- [esptool v5 동작·검증](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/esptool/basic-commands.html)
- [준비한 esptool 5.3.0 공식 배포](https://github.com/espressif/esptool/releases/tag/v5.3.0)
- [INA219 제어 전압·Vin+/Vin-/공통 접지](https://learn.adafruit.com/adafruit-ina219-current-sensor-breakout/wiring)
- [CP210x 공식 드라이버](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers)
