# RS232 to MQTT Bridge - ESP32-S3

RS232 데이터를 수신하여 MQTT로 전송하는 ESP32-S3 기반 브릿지 펌웨어입니다.
BLE를 통해 스마트폰 앱에서 설정하고 모니터링할 수 있습니다.

## 📋 주요 기능

- **RS232 데이터 수신**: 다양한 통신 속도 지원 (2400 ~ 921600 bps)
- **프로토콜 지원**: Custom, Modbus RTU/ASCII, NMEA 0183, IEC 60870-5
- **데이터 파싱**: 사용자 정의 데이터 필드 파싱 (최대 64개 필드)
- **MQTT 전송**: WiFi를 통한 MQTT 서버 전송 (TLS 지원)
- **BLE 설정**: 스마트폰 앱을 통한 무선 설정
- **실시간 모니터링**: BLE 및 MQTT를 통한 데이터 모니터링

## 🛠 하드웨어 요구사항

- ESP32-S3-WROOM-1-N8R8 또는 호환 모듈
- MAX3232 RS232 레벨 변환기
- 3.3V 전원 공급

### 핀 연결

| ESP32-S3 핀 | 기능 | 연결 |
|------------|------|------|
| GPIO17 | UART TX | MAX3232 T1IN |
| GPIO18 | UART RX | MAX3232 R1OUT |

## 💻 빌드 환경

### ESP-IDF 설치 (v5.0 이상)

```bash
# ESP-IDF 설치
git clone -b v5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source export.sh
```

### 프로젝트 빌드

```bash
cd esp32_rs232_mqtt
idf.py set-target esp32s3
idf.py build
```

### 펌웨어 플래싱

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## 📁 프로젝트 구조

```
esp32_rs232_mqtt/
├── main/
│   ├── main.c              # 메인 애플리케이션
│   ├── protocol_def.h      # 프로토콜 정의
│   ├── ble_service.c/h     # BLE GATT 서비스
│   ├── wifi_manager.c/h    # WiFi 연결 관리
│   ├── mqtt_handler.c/h    # MQTT 클라이언트
│   ├── uart_handler.c/h    # UART 데이터 수신
│   ├── data_parser.c/h     # 데이터 파싱
│   ├── nvs_storage.c/h     # NVS 설정 저장
│   ├── crc_utils.c/h       # CRC 계산
│   └── cmd_handler.c/h     # BLE 명령 처리
├── components/             # 컴포넌트 (선택적)
├── CMakeLists.txt
├── partitions.csv
└── sdkconfig.defaults
```

## 📱 BLE 서비스

### Service UUID
`4fafc201-1fb5-459e-8fcc-c5c9c331914b`

### Characteristics

| 이름 | UUID 끝자리 | 속성 | 설명 |
|------|------------|------|------|
| WiFi Config | ...26a8 | Write | WiFi SSID/Password 설정 |
| MQTT Config | ...26a9 | Write | MQTT 서버 설정 |
| Protocol Config | ...26aa | Write | 시리얼 프로토콜 설정 |
| UART Config | ...26ab | Write | 통신 속도 설정 |
| Data Definition | ...26ac | Write | 데이터 필드 정의 |
| Device Status | ...26ad | Read/Notify | 장치 상태 |
| Parsed Data | ...26ae | Notify | 파싱된 데이터 |
| Command | ...26af | Write | 제어 명령 |

## 📡 MQTT 토픽

```
{base_topic}/
├── data        # 파싱된 데이터 (JSON)
├── status      # 장치 상태 (JSON)
├── cmd         # 명령 수신 (구독)
└── response    # 명령 응답
```

### 데이터 메시지 예시

```json
{
  "device_id": "ESP32_ABCD1234",
  "timestamp": 1706102400,
  "sequence": 12345,
  "raw_hex": "02A1B2C3D403",
  "fields": {
    "Temperature": { "value": 25.5, "type": "FLOAT32" },
    "Humidity": { "value": 65, "type": "UINT8" }
  }
}
```

## ⚙️ 설정 절차

1. **ESP32에 전원 공급**
2. **스마트폰 앱으로 BLE 연결** (장치명: RS232_MQTT_Bridge)
3. **WiFi 설정**: SSID/Password 입력
4. **MQTT 설정**: 브로커 주소, 포트, 인증정보, 토픽 설정
5. **UART 설정**: 통신 속도 설정 (기본 115200)
6. **프로토콜 설정**: Custom/Modbus/NMEA 등 선택
7. **데이터 정의**: 파싱할 필드 정의

## 🔧 지원 데이터 타입

| 코드 | 타입 | 크기 |
|------|------|------|
| 0x00 | BOOL | 1 bit |
| 0x01 | UINT8 | 1 byte |
| 0x02 | INT8 | 1 byte |
| 0x03 | UINT16 | 2 bytes |
| 0x04 | INT16 | 2 bytes |
| 0x05 | UINT32 | 4 bytes |
| 0x06 | INT32 | 4 bytes |
| 0x10 | FLOAT32 | 4 bytes |
| 0x11 | FLOAT64 | 8 bytes |
| 0x30 | STRING | N bytes |

## 🔒 지원 CRC 타입

- None
- XOR (LRC)
- Sum8, Sum16
- CRC-8, CRC-8-CCITT
- CRC-16-IBM, CRC-16-CCITT, CRC-16-Modbus, CRC-16-XMODEM
- CRC-32, CRC-32-C

## 📜 버전 히스토리

| 버전 | 날짜 | 변경사항 |
|------|------|---------|
| 1.0.0 | 2026-01-24 | 초기 릴리즈 |
| 1.1.0 | 2026-01-27 | OTA 업데이트 지원 추가 |

## 🔄 OTA (Over-The-Air) 업데이트

### 기능
- **GitHub Releases 연동**: 자동 버전 확인 및 다운로드
- **BLE 트리거**: 스마트폰 앱에서 수동으로 업데이트 시작
- **롤백 지원**: 업데이트 실패 시 이전 버전으로 자동 복구
- **진행률 알림**: 실시간 다운로드/설치 진행률 BLE 알림
- **펌웨어 서명**: 프로덕션용 서명 검증 지원

### OTA 명령어 (BLE)

| 명령 코드 | 이름 | 설명 |
|-----------|------|------|
| 0x10 | CMD_OTA_CHECK | 새 버전 확인 |
| 0x11 | CMD_OTA_START | 업데이트 시작 |
| 0x12 | CMD_OTA_ABORT | 업데이트 중단 |
| 0x13 | CMD_OTA_ROLLBACK | 이전 버전으로 롤백 |
| 0x14 | CMD_OTA_GET_VERSION | 현재 버전 정보 요청 |

### 앱에서 OTA 사용법

1. **버전 확인**: `CMD_OTA_CHECK` 전송
2. **응답 수신**: `{"current":"1.0.0","latest":"1.1.0","update":true}`
3. **업데이트 시작**: `CMD_OTA_START` 전송
4. **진행률 수신**: `{"state":"downloading","progress":50}`
5. **완료**: 자동 재부팅

### GitHub 저장소 설정

1. **version.json 파일 구조**:
```json
{
  "version": "1.1.0",
  "url": "https://github.com/YOUR_USER/YOUR_REPO/releases/download/v1.1.0/firmware_v1.1.0.bin",
  "size": 1234567,
  "release_date": "2026-01-27T00:00:00Z"
}
```

2. **GitHub Actions**: 태그 푸시 시 자동 빌드 및 릴리즈
```bash
git tag v1.1.0
git push origin v1.1.0
```

3. **ota_handler.c 수정**: `OTA_VERSION_URL`을 실제 저장소 URL로 변경
```c
#define OTA_VERSION_URL "https://raw.githubusercontent.com/YOUR_USER/YOUR_REPO/main/firmware/version.json"
```

### 파티션 구성 (16MB Flash)

| 파티션 | 타입 | 오프셋 | 크기 | 설명 |
|--------|------|--------|------|------|
| nvs | data | 0x9000 | 24KB | 설정 저장 |
| otadata | data | 0xF000 | 8KB | OTA 부팅 정보 |
| phy_init | data | 0x11000 | 4KB | PHY 초기화 |
| ota_0 | app | 0x20000 | 3.5MB | 앱 파티션 1 |
| ota_1 | app | 0x3A0000 | 3.5MB | 앱 파티션 2 |
| storage | data | 0x720000 | 512KB | SPIFFS 저장소 |
| coredump | data | 0x7A0000 | 64KB | 코어 덤프 |

### 프로덕션 빌드 (Secure Boot)

프로덕션 배포 시 Secure Boot를 활성화하려면 `sdkconfig.defaults`에서 주석 해제:

```
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y
CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y
```

자세한 내용은 [ESP-IDF Secure Boot 문서](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/secure-boot-v2.html) 참조.

## 📄 라이선스

MIT License

## 🤝 기여

이슈 및 PR 환영합니다!
