# 🛡️ SmartBox Git 히스토리 민감 정보 영구 삭제 가이드 (SECURITY_CLEANUP_GUIDE)

이 가이드는 Git 커밋 히스토리에 남아 있는 하드코딩된 민감 정보(Secrets)를 완전히 세탁하고 영구 삭제하기 위한 지침입니다. 소스 코드 리팩토링 및 파일 수정만으로는 과거 커밋 기록에 남아 있는 보안 정보 유출을 완벽하게 막을 수 없습니다.

다음 중 선호하는 도구(**git-filter-repo** 또는 **BFG Repo-Cleaner**)를 선택하여 히스토리를 세탁하십시오.

---

> [!CAUTION]
> **중요 안전 조치:** 
> - Git 히스토리를 재작성(Rewrite)하면 커밋 해시(Hash)가 변경됩니다.
> - 작업을 시작하기 전에 반드시 현재 레포지토리 폴더를 통째로 백업하거나 다른 경로로 복사해 두십시오.
> - 작업 완료 후 GitHub 등 원격 레포지토리에 반영하려면 반드시 강제 푸시(`git push origin --force --all`)를 수행해야 합니다. team원들과 협업 중인 경우 주의하십시오.

---

## 📂 1단계: 치환 대상 파일 (`replacements.txt`) 생성

동일한 디렉터리에 `replacements.txt` 파일을 생성하고 아래 내용을 입력합니다. 이 파일은 Git 히스토리 세탁 작업이 끝나면 반드시 삭제해야 합니다.

```text
***REMOVED***==>***REMOVED***
***REMOVED***==>***REMOVED***
***REMOVED***==>***REMOVED***
***REMOVED***==>***REMOVED***
***REMOVED***==>***REMOVED***
```

---

## 🛠️ 2단계: 도구별 실행 방법

### 방법 A: `git-filter-repo` 사용 (Git 공식 권장 방식)

`git-filter-repo`는 가장 현대적이고 강력한 Git 히스토리 필터링 도구입니다.

#### 1. 설치 방법 (Python 및 Git 필요)
- **Windows (PowerShell):**
  ```powershell
  pip install git-filter-repo
  ```
- **macOS / Linux:**
  ```bash
  brew install git-filter-repo
  ```

#### 2. 명령어 실행
레포지토리 루트 디렉터리에서 다음 명령어를 실행합니다:
```bash
git filter-repo --replace-text replacements.txt --force
```

#### 3. 정리 작업
- `git-filter-repo`가 완료되면 로컬 레포지토리의 원격(Origin) 주소 바인딩 정보가 초기화될 수 있습니다. 다음 명령으로 원격 레포지토리를 다시 추가하십시오:
  ```bash
  git remote add origin [YOUR_GITHUB_REPO_URL]
  ```

---

### 방법 B: `BFG Repo-Cleaner` 사용 (간편하고 빠른 자바 기반 방식)

`BFG Repo-Cleaner`는 대규모 리포지토리를 정제할 때 매우 빠르고 직관적입니다.

#### 1. 요구 사항
- 시스템에 Java Runtime Environment (JRE)가 설치되어 있어야 합니다.
- [BFG 공식 사이트](https://rtyley.github.io/bfg-repo-cleaner/)에서 `bfg.jar` 파일을 다운로드하여 리포지토리 폴더에 둡니다.

#### 2. 명령어 실행
- `replacements.txt` 형식은 BFG의 경우 단순 문자열 나열도 가능하나, 다음과 같이 작성합니다 (BFG는 좌측 문자열을 우측 문자열로 대체합니다).
  ```text
  ***REMOVED***==>***REMOVED***
  ***REMOVED***==>***REMOVED***
  ***REMOVED***==>***REMOVED***
  ***REMOVED***==>***REMOVED***
  ***REMOVED***==>***REMOVED***
  ```
- 다음 명령어를 실행하여 치환 작업을 진행합니다:
  ```bash
  java -jar bfg.jar --replace-text replacements.txt
  ```

#### 3. 불필요한 Git 캐시 및 객체 물리적 소거
BFG 실행 후, 실제 디스크 공간 및 Git 내부의 레퍼런스를 소거해야 완전히 세탁됩니다.
```bash
git reflog expire --expire=now --all && git gc --prune=now --aggressive
```

---

## 🧹 3단계: 마무리 및 검증

1. **치환 대상 텍스트 파일 삭제**
   - 레포지토리에 생성했던 `replacements.txt`와 다운로드한 `bfg.jar` 파일을 완전히 지웁니다:
     ```bash
     rm replacements.txt
     ```
2. **검증**
   - `git log -p -S "tworimpa"` 또는 `git log -p -S "KfzN6"` 명령을 실행하여 예전 커밋 로그에서 해당 내용이 `***REMOVED***`로 성공적으로 바뀌었는지 확인합니다.
3. **원격 저장소에 동기화**
   - 세탁된 로컬 브랜치를 원격(GitHub 등)에 강제로 업로드합니다:
     ```bash
     git push origin --force --all
     git push origin --tags --force
     ```
