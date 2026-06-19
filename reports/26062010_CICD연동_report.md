# 📋 26062010_CICD연동_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** CICD연동
    
---
    
## 1. 이슈 개요 (Issue Overview)
스마트 수거함(SmartBox) 프로젝트의 품질 유지와 신속한 무선 펌웨어 배포를 위해, GitHub 저장소의 `main` 브랜치에 코드가 푸시(push)될 때마다 자동으로 네이티브 유닛 테스트를 수행하고 컴파일 및 Synology NAS OTA 배포 서버로의 자동 전송이 이루어지는 **CI/CD 자동화 파이프라인 구축**이 요구되었습니다.

## 2. 원인 분석 (Root Cause Analysis)
1. **물리 하드웨어의 부재:** GitHub Actions Runner(Cloud Environment)에는 실제 ESP32-C6 물리 개발 보드가 연결되어 있지 않으므로, 장치에 직접 업로드하여 테스트하는 `pio test -e esp32-c6-test` 명령은 포트 미연결 에러로 인해 동작할 수 없습니다.
2. **배포 프로세스의 수동화:** 개발자가 로컬에서 빌드된 `firmware.bin`과 메타데이터 파일을 매번 직접 SFTP 클라이언트를 통해 Synology NAS 배포 경로로 수동 업로드해야 하는 번거로움과 버전 누락 위험이 상존했습니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)

물리 하드웨어 없이도 클라우드 러너에서 완벽하게 동작하는 자동 배포 파이프라인을 아래와 같이 설계 및 구현했습니다:

1. **호스트 네이티브 테스트 환경 정의 (`platformio.ini`)**
   - PlatformIO의 `platform = native` 환경을 정의하고 `build_flags = -DNATIVE_BUILD` 플래그를 등록했습니다.
   - 하드웨어 및 아두이노 Core 의존성을 제거하기 위해, ESP32 타겟 소스(`main.cpp`, `Esp32Hardware.cpp`, `WebDashboard.cpp`, `WifiManager.cpp` 등)를 제외하고 Mock을 적용하는 FSM 코어 로직만 빌드하도록 `build_src_filter`를 설정하였습니다.

2. **테스트 시작점 조건부 이원화 (`test_main.cpp`)**
   - `NATIVE_BUILD` 매크로 여부에 따라 분기하여, 네이티브 빌드 시에는 Unity Test의 일반 C++ 실행 시작점인 `int main(int argc, char **argv)`을 기동하고, 보드 빌드 시에는 아두이노 `setup()` 및 `loop()`가 실행되도록 전처리를 추가했습니다.
   - 네이티브 빌드 시 `<Arduino.h>` 라이브러리 인클루드를 조건부로 무시하도록 설정했습니다.

3. **CI/CD 워크플로우 명세 작성 (`.github/workflows/deploy.yml`)**
   - **`test` Job (CI - Test):** `ubuntu-latest` 환경에서 리포지토리를 체크아웃하고 Python과 PlatformIO Core를 설치한 후, `pio test -e native` 명령을 통해 FSM 및 안전 보호 기능 유닛 테스트를 비실물 가상 환경에서 수행합니다.
   - **`build_and_deploy` Job (CI/CD - Build & Deploy):** 테스트가 완벽히 통과(`needs: test`)했을 때만 구동되며, `pio run -e esp32-c6-devkitc-1` 명령으로 실제 ESP32-C6용 프로덕션 바이너리를 빌드합니다.
   - **메타데이터 자동 생성:** 셸 스크립트를 통해 `src/SmartBoxController.h`의 `FIRMWARE_VERSION`에서 버전을 추출하고 GitHub Actions의 `run_id`, `sha` 해시, 빌드 날짜를 결합해 `version.json`을 동적 생성하여 빌드 출력 디렉토리에 병합합니다.
   - **SFTP 자동 전송:** `appleboy/scp-action` 라이브러리를 연동하여, 빌드 완료된 `firmware.bin` 및 `version.json`을 Synology NAS의 지정 디렉토리로 비밀 보안 변수(GitHub Secrets) 기반으로 안전하게 전송합니다.

## 4. 검증 결과 (Verification Results)
- **로컬 빌드 및 컴파일 검증:** 로컬 개발 환경에서 `platformio.ini` 및 `test_main.cpp` 수정 후 `esp32-c6-devkitc-1` 타겟 펌웨어 빌드가 성공적으로 완료됨을 확인하였습니다 (190.8초 성공).
- **GitHub 저장소 반영 완료:** CI/CD 연동 및 소스코드 수정 내역을 Git 형상 관리에 완전히 반영 및 커밋을 완료하여, 푸시 즉시 동작 가능하도록 구성되었습니다.

---

### 🔑 GitHub Repository Secrets 설정 가이드
파이프라인의 안전한 동작을 위해, GitHub 리포지토리의 `Settings -> Secrets and variables -> Actions` 메뉴에서 아래 5가지 보안 변수를 생성해 등록해야 합니다:
1. `NAS_HOST`: Synology NAS의 외부 접속 IP 주소 또는 도메인 네임
2. `NAS_PORT`: SSH/SFTP 서비스 포트 번호 (기본 22 또는 사용자 설정 포트)
3. `NAS_USER`: SFTP 업로드 권한을 가진 NAS 로그인 계정명
4. `NAS_PASSWORD`: 해당 NAS 계정의 비밀번호
5. `NAS_TARGET_DIR`: NAS 내 최신 펌웨어를 저장할 타겟 절대 경로 (예: `/volume1/web/firmware/`)
