# GitHub 저장소 설정 가이드

## AI-sunwoo/rs232-mqtt-bridge 저장소 설정

### 📋 사전 준비

1. GitHub 계정 로그인: https://github.com/AI-sunwoo
2. Git 설치 확인: `git --version`
3. ESP-IDF 환경 설정 완료

---

## 🚀 Step 1: GitHub 저장소 생성

### 1.1 웹에서 저장소 생성

1. https://github.com/new 접속
2. 다음 정보 입력:
   - **Repository name**: `rs232-mqtt-bridge`
   - **Description**: `RS232 to MQTT Bridge - ESP32-S3 IoT Gateway`
   - **Visibility**: `Public` (또는 Private)
   - **Add a README file**: 체크 해제 (이미 있음)
   - **Add .gitignore**: 체크 해제
   - **Choose a license**: None (나중에 추가)
3. **Create repository** 클릭

---

## 🚀 Step 2: 로컬 저장소 설정

### 2.1 프로젝트 폴더로 이동

```cmd
cd C:\Users\LSW\esp32_rs232_mqtt
```

### 2.2 Git 초기화 및 원격 저장소 연결

```cmd
:: Git 초기화
git init

:: 사용자 정보 설정 (처음 한 번만)
git config user.name "AI-sunwoo"
git config user.email "your-email@example.com"

:: 원격 저장소 연결
git remote add origin https://github.com/AI-sunwoo/rs232-mqtt-bridge.git

:: 현재 브랜치를 main으로 설정
git branch -M main
```

### 2.3 .gitignore 생성

```cmd
:: .gitignore 파일 생성
echo # Build outputs > .gitignore
echo build/ >> .gitignore
echo sdkconfig >> .gitignore
echo sdkconfig.old >> .gitignore
echo. >> .gitignore
echo # IDE >> .gitignore
echo .vscode/ >> .gitignore
echo .idea/ >> .gitignore
echo *.swp >> .gitignore
echo. >> .gitignore
echo # OS >> .gitignore
echo .DS_Store >> .gitignore
echo Thumbs.db >> .gitignore
```

### 2.4 첫 커밋 및 푸시

```cmd
:: 모든 파일 스테이징
git add .

:: 첫 커밋
git commit -m "Initial commit: RS232 to MQTT Bridge with OTA support"

:: GitHub에 푸시
git push -u origin main
```

> **인증 팝업이 뜨면**: GitHub 계정으로 로그인하거나, Personal Access Token 입력

---

## 🚀 Step 3: GitHub Actions 권한 설정

### 3.1 Actions 권한 활성화

1. 저장소 페이지로 이동: https://github.com/AI-sunwoo/rs232-mqtt-bridge
2. **Settings** 탭 클릭
3. 왼쪽 메뉴에서 **Actions** → **General** 클릭
4. **Workflow permissions** 섹션에서:
   - ✅ **Read and write permissions** 선택
   - ✅ **Allow GitHub Actions to create and approve pull requests** 체크
5. **Save** 클릭

---

## 🚀 Step 4: 첫 번째 릴리즈 생성

### 4.1 태그 생성 및 푸시

```cmd
:: 버전 태그 생성
git tag -a v1.0.0 -m "Initial release with OTA support"

:: 태그 푸시 (이때 GitHub Actions가 자동 실행됨)
git push origin v1.0.0
```

### 4.2 GitHub Actions 빌드 확인

1. https://github.com/AI-sunwoo/rs232-mqtt-bridge/actions 접속
2. "ESP32 Firmware Build & Release" 워크플로우 실행 확인
3. 빌드 완료까지 약 5-10분 소요

### 4.3 릴리즈 확인

1. https://github.com/AI-sunwoo/rs232-mqtt-bridge/releases 접속
2. v1.0.0 릴리즈에 다음 파일 확인:
   - `firmware_v1.0.0.bin` - 펌웨어 바이너리
   - `firmware_v1.0.0.sha256` - 체크섬
   - `version.json` - 버전 정보

---

## 🚀 Step 5: OTA 테스트

### 5.1 version.json URL 확인

브라우저에서 다음 URL 접속:
```
https://raw.githubusercontent.com/AI-sunwoo/rs232-mqtt-bridge/main/firmware/version.json
```

다음과 같은 JSON이 표시되어야 함:
```json
{
  "version": "1.0.0",
  "url": "https://github.com/AI-sunwoo/rs232-mqtt-bridge/releases/download/v1.0.0/firmware_v1.0.0.bin",
  "size": 1234567,
  "release_date": "2026-01-27T00:00:00Z"
}
```

### 5.2 ESP32에서 OTA 테스트

1. ESP32 펌웨어 빌드 및 플래싱
2. WiFi 연결 설정
3. BLE 앱에서 OTA 버전 확인 명령 (0x10) 전송
4. 응답 확인

---

## 📦 향후 업데이트 방법

### 새 버전 릴리즈 (예: v1.1.0)

```cmd
:: 1. 코드 수정 후 커밋
git add .
git commit -m "Add new feature XYZ"
git push origin main

:: 2. CMakeLists.txt 버전 업데이트
:: set(PROJECT_VER "1.1.0") 로 변경

:: 3. 변경사항 커밋
git add CMakeLists.txt
git commit -m "Bump version to 1.1.0"
git push origin main

:: 4. 새 태그 생성 및 푸시
git tag -a v1.1.0 -m "Version 1.1.0 - New feature XYZ"
git push origin v1.1.0
```

GitHub Actions가 자동으로:
1. 펌웨어 빌드
2. Releases에 업로드
3. version.json 업데이트

---

## 🔧 문제 해결

### 인증 오류 시

```cmd
:: Personal Access Token 사용
git remote set-url origin https://AI-sunwoo:YOUR_TOKEN@github.com/AI-sunwoo/rs232-mqtt-bridge.git
```

또는 GitHub CLI 사용:
```cmd
:: GitHub CLI 설치 후
gh auth login
```

### Actions 빌드 실패 시

1. Actions 탭에서 실패한 워크플로우 클릭
2. 로그 확인
3. 일반적인 원인:
   - ESP-IDF 버전 불일치
   - 빌드 에러 (코드 문제)
   - 권한 문제 (Settings에서 확인)

### version.json 접근 불가 시

1. 저장소가 Public인지 확인
2. 파일 경로 확인: `firmware/version.json`
3. Raw URL 형식 확인: `https://raw.githubusercontent.com/...`

---

## 📁 저장소 구조

```
rs232-mqtt-bridge/
├── .github/
│   └── workflows/
│       └── release.yml          # CI/CD 워크플로우
├── firmware/
│   └── version.json             # OTA 버전 정보
├── main/
│   ├── main.c
│   ├── ota_handler.c            # OTA 핸들러
│   ├── ble_service.c
│   └── ...
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── README.md
└── .gitignore
```

---

## 🔗 주요 URL

| 항목 | URL |
|------|-----|
| 저장소 | https://github.com/AI-sunwoo/rs232-mqtt-bridge |
| Releases | https://github.com/AI-sunwoo/rs232-mqtt-bridge/releases |
| Actions | https://github.com/AI-sunwoo/rs232-mqtt-bridge/actions |
| version.json | https://raw.githubusercontent.com/AI-sunwoo/rs232-mqtt-bridge/main/firmware/version.json |

---

## ✅ 체크리스트

- [ ] GitHub 저장소 생성
- [ ] 로컬 Git 초기화
- [ ] 원격 저장소 연결
- [ ] .gitignore 생성
- [ ] 첫 커밋 & 푸시
- [ ] Actions 권한 설정
- [ ] v1.0.0 태그 생성 & 푸시
- [ ] Actions 빌드 성공 확인
- [ ] Releases 파일 확인
- [ ] version.json URL 접근 확인
- [ ] ESP32 OTA 테스트
