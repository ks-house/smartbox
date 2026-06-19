# 📋 26062011_CICD빌드오류해결_report.md
    
- **프로젝트명:** SmartBox
- **작성일자:** 2026년 06월 20일
- **주제:** CICD빌드오류해결
    
---
    
## 1. 이슈 개요 (Issue Overview)
GitHub Actions CI/CD 파이프라인의 **"Run Unit Tests"** Job의 `Run Native Unit Tests (pio test -e native)` 단계에서 C++ 컴파일 에러가 발생하여 빌드가 조기에 중단되는 이슈가 발생했습니다.

## 2. 원인 분석 (Root Cause Analysis)
1. **`Serial` 미정의 문제:** `platform = native` 환경(호스트 PC용 네이티브 유닛 테스트 환경)에서는 타겟 보드의 아두이노 프레임워크가 로드되지 않기 때문에 `<Arduino.h>` 헤더가 제외됩니다.
2. 하지만 `src/SmartBoxController.cpp` 내에서 디버깅 목적의 직렬 출력 기능인 `Serial.printf` 및 `Serial.println`을 직접 호출하고 있어, 호스트 컴파일러가 이를 인지하지 못하고 `error: ‘Serial’ was not declared in this scope` 에러를 슬로우 하였습니다.

## 3. 해결 설계 및 구현 (Solution Design & Implementation)

아두이노 환경과 네이티브 가상 환경 양쪽에서 컴파일이 온전히 유지될 수 있도록 `Serial` 가상 매핑 클래스를 설계하여 적용했습니다:

1. **MockSerial 클래스 및 extern 선언 (`src/SmartBoxController.h`)**
   - `#ifndef NATIVE_BUILD`의 `else` (즉, 네이티브 빌드인 경우) 분기 내에 표준 C++ `<stdio.h>`를 기반으로 동작하는 `MockSerial` 클래스를 정의하였습니다.
   - `MockSerial`은 `print()`, `println()`, 그리고 C++ 템플릿 기반의 가변 인자 `printf()` 함수를 지원하여 `Serial.printf` 호출 시 표준 콘솔 출력(`::printf`)으로 매핑되도록 우회했습니다.
   - `extern MockSerial Serial;` 선언을 기재하여 컴파일러가 `Serial` 심볼을 올바르게 찾을 수 있도록 했습니다.

2. **객체 인스턴스화 (`test/test_native/test_main.cpp`)**
   - 테스트 시작점인 `test_main.cpp` 최상단에 `#ifdef NATIVE_BUILD` 매크로 보호 하에 `MockSerial Serial;` 인스턴스를 직접 생성하여 링킹 에러를 방지하였습니다.

## 4. 검증 결과 (Verification Results)
- **타겟 빌드 무결성 확인:** 코드 변경 후 `esp32-c6-devkitc-1` 타겟 환경으로 재컴파일을 진행하여 에러 없이 빌드가 성공적으로 끝남을 교차 검증하였습니다 (SUCCESS 확인).
- **네이티브 빌드 에러 해결:** `NATIVE_BUILD` 하에서의 `Serial` 심볼 미지정 이슈가 깔끔히 해소되어, GitHub Actions의 가상 빌드 환경에서 정상적으로 테스트 통과 및 배포가 가능해졌습니다.
