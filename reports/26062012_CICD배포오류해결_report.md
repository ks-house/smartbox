# 📋 26062012_CICD배포오류해결_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** CICD배포오류해결
    
---
    
## 1. 이슈 개요 (Issue Overview)
GitHub Actions CI/CD 파이프라인에서 빌드 완료 후 Synology NAS 배포 서버로 `firmware.bin` 및 `version.json` 파일을 전송하는 **"Deploy to Synology NAS via SFTP"** 단계가 계속 실패하는 문제가 발생했습니다.

## 2. 원인 분석 (Root Cause Analysis)
1. **SSH 명령 실행 제한 (SCP 사용 불가):** 
   - Synology NAS 배포용 계정이 보안상의 이유로 SFTP 전용으로 설정되어 셸 명령 실행 권한이 없었습니다.
   - 기존의 `appleboy/scp-action`은 내부적으로 원격 셸을 통해 `tar` 등을 실행하므로 `Process exited with status 1` 에러로 실패했습니다.
2. **디렉토리 생성 권한 및 SFTP 루트 경로(Chroot) 매핑 불일치:**
   - `wlixcc/SFTP-Deploy-Action`으로 전환한 후에도, `local_path`를 빌드 디렉토리 자체(`./.pio/build/esp32-c6-devkitc-1/`)로 지정했을 때 액션이 하위 디렉토리를 원격에 생성하려 했으나, `sftp_only: true` 옵션으로 인해 원격 디렉토리 생성(`mkdir`) 기능이 동작하지 않아 실패했습니다.
   - 또한 `remote_path`를 `.`으로 지정했을 때, SFTP 로그인 루트 경로 `/`가 실제 NAS의 `/volume1/web/firmware`로 매핑되어 있는 chroot 상황이었기 때문에, 액션이 `/./firmware.bin` 즉 루트 `/` 디렉토리에 직접 쓰기를 시도하여 `Permission denied` 에러가 발생했습니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)
1. **빌드 산출물 격리 (Prepare deployment directory):**
   - 빌드용 디렉토리 내부의 무수한 임시 오브젝트 파일과 설정 파일들을 배포 대상에서 배제하기 위해, 배포용으로 필요한 핵심 바이너리(`firmware.bin`)와 메타데이터(`version.json`)만 별도의 임시 폴더인 `./dist/`를 생성하여 복사하도록 단계를 추가했습니다.
2. **원격 절대 경로 설정 및 대상 파일 명시:**
   - `.github/workflows/deploy.yml`에서 `local_path`를 `./dist/*`로 지정하여 폴더 내부의 파일들만 직접 업로드하도록 하고, `remote_path`를 `${{ secrets.NAS_TARGET_DIR }}` (사용자가 설정한 절대 경로 `/volume1/web/firmware/`)로 설정했습니다.
   - 이렇게 하면 SFTP가 이미 존재하는 디렉토리 안에 파일만 쓰기 때문에 별도의 디렉토리 생성이 필요 없어 `sftp_only: true` 제약 조건 하에서도 오류 없이 깔끔히 전송됩니다.

## 4. 검증 결과 (Verification Results)
- 수정된 워크플로우 명세서를 Git에 커밋 후 `main` 브랜치에 푸시하여 GitHub Actions 빌드(Run ID: `27840031192`)가 정상적으로 가동되었습니다.
- Native 유닛 테스트 통과 -> ESP32-C6 펌웨어 빌드 성공 -> `version.json` 동적 생성 -> SFTP를 통한 최종 업로드 성공(Deploy Success)까지 일련의 파이프라인이 단 한 번의 오류 없이 완벽하게 실행 완료됨을 교차 검증하였습니다.
