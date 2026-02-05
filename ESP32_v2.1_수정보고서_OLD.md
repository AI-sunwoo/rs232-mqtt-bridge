# ESP32 펌웨어 v2.1 수정 보고서

## 통합 인터페이스 정의서 v2.1 적용 결과

### 📋 수정된 파일 목록

| 파일 | 수정 내용 | P0 이슈 |
|------|----------|---------|
| `protocol_def.h` | 타입 정의 전면 개편, 신규 필드 추가 | P0-1, P0-2, P0-3 |
| `mqtt_handler.c` | MQTT 토픽, 메시지, 원격 명령 처리 | P0-1, P0-3 |
| `mqtt_handler.h` | 함수 시그니처 확장 | P0-1, P0-3 |
| `cmd_handler.c` | BLE 패킷 파싱 로직 수정, cJSON 포함 추가 | P0-1, P0-2 |
| `cmd_handler.h` | 원격 명령 핸들러 추가 | P0-3 |
| `nvs_storage.c` | 신규 필드 저장/로드 | P0-1, P0-2 |
| `nvs_storage.h` | config_hash 함수 추가 | P0-3 |
| `main.c` | 콜백 연결, status 확장, cmd_handler.h 포함 추가 | P0-3 |

---

## 🔧 검토 중 발견된 버그 수정

### Bug Fix 1: Static 변수 extern 접근 불가
**문제**: `main.c`의 전역 설정 변수들이 `static`으로 선언되어 `cmd_handler.c`에서 `extern`으로 접근 불가

**수정**:
```c
// Before
static device_status_t g_device_status = {0};
static wifi_config_data_t g_wifi_config = {0};
// ...

// After
device_status_t g_device_status = {0};  // Non-static for extern access
wifi_config_data_t g_wifi_config = {0};
// ...
```

### Bug Fix 2: mqtt_handler_publish_data 파라미터 누락
**문제**: `main.c`에서 `mqtt_handler_publish_data()` 호출 시 v2.1에서 추가된 `crc_valid` 파라미터 누락

**수정**:
```c
// Before
mqtt_handler_publish_data(g_device_id, fields, field_count,
                          item.data, item.length, g_sequence);

// After
bool crc_valid = true;  // v2.1: CRC validation result
mqtt_handler_publish_data(g_device_id, fields, field_count,
                          item.data, item.length, g_sequence, crc_valid);
```

### Bug Fix 3: cJSON.h 누락
**문제**: `cmd_handler.c`에서 `cJSON` 타입 사용하지만 헤더 포함 안됨

**수정**: `#include "cJSON.h"` 추가

### Bug Fix 4: cmd_handler.h 누락
**문제**: `main.c`에서 `cmd_handler_process_remote` 사용하지만 헤더 포함 안됨

**수정**: `#include "cmd_handler.h"` 추가

---

## 🔧 컴파일 오류 수정 (2차 검토)

### Compilation Fix 1: 버퍼 오버플로우 (ble_service.c)
**오류 메시지**:
```
error: array subscript 42 is above array bounds of 'uint8_t[32]'
```

**원인**: v2.1에서 `device_status_t` 구조체가 확장됨
- v1.0: ~25 bytes
- v2.1: ~34 bytes (free_heap + config_hash 추가)

**수정**: `ble_service_notify_status()`의 packet 버퍼를 32 → 64 bytes로 확장

```c
// Before
uint8_t packet[32];

// After  
uint8_t packet[64];  // v2.1: device_status_t increased
```

### Compilation Fix 2: 미사용 변수 경고 (ota_handler.c)
**경고 메시지**:
```
warning: unused variable 'ota_state'
```

**수정**: `ota_handler_can_rollback()`에서 사용하지 않는 `ota_state` 변수 제거

---

## P0-1: userId/deviceId/baseTopic 필수화

### 변경 전 (v1.0)
```c
// MQTT 토픽이 옵션이었음
typedef struct {
    char topic[128];        // Legacy, optional
    char user_id[64];       // optional
    char device_id[32];     // optional
    // ...
} mqtt_config_data_t;
```

### 변경 후 (v2.1)
```c
typedef struct {
    // ... 기존 필드 ...
    
    // v2.1 필수 필드 (P0-1)
    char user_id[MQTT_USER_ID_MAX_LEN + 1];     // ★ 필수
    char device_id[MQTT_DEVICE_ID_MAX_LEN + 1]; // ★ 필수
    char base_topic[MQTT_BASE_TOPIC_MAX_LEN + 1]; // ★ 필수
    
    bool use_jwt;  // P0-2
} mqtt_config_data_t;
```

### BLE 패킷 파싱 순서 (cmd_handler.c)
```
1. broker_len(1) + broker
2. port(2, LE)
3. username_len(1) + username
4. password_len(2, LE) + password  ← v2.1: 2바이트로 확장 (JWT 지원)
5. client_id_len(1) + client_id
6. user_id_len(1) + user_id        ★ P0-1: 필수
7. device_id_len(1) + device_id    ★ P0-1: 필수
8. base_topic_len(1) + base_topic  ★ P0-1: 필수
9. qos(1)
10. use_tls(1)
11. use_jwt(1)                      ★ P0-2: 추가
```

### MQTT 토픽 생성 로직
```c
// mqtt_handler.c - build_topic()
if (strlen(s_config.user_id) > 0 && strlen(s_config.device_id) > 0) {
    // v2.1 SaaS format: user/{user_id}/device/{device_id}/{suffix}
    snprintf(out, out_size, "user/%s/device/%s/%s",
             s_config.user_id, s_config.device_id, suffix);
} else if (strlen(s_config.base_topic) > 0) {
    // base_topic 사용
    snprintf(out, out_size, "%s/%s", s_config.base_topic, suffix);
}
```

---

## P0-2: JWT 토큰 인증 지원

### 변경 내용
1. `password` 필드 크기 확장: 64 → 512 bytes (JWT 토큰 저장)
2. `use_jwt` 플래그 추가
3. BLE 패킷에서 password 길이를 2바이트로 변경

### NVS 저장
```c
nvs_set_u8(handle, "use_jwt", config->use_jwt ? 1 : 0);
```

---

## P0-3: 원격 명령 처리 및 설정 동기화

### 구독 토픽 (MQTT 연결 시)
```c
// 1. cmd 토픽 (원격 명령)
"user/{user_id}/device/{device_id}/cmd"

// 2. config/download 토픽 (설정 동기화)
"user/{user_id}/device/{device_id}/config/download"
```

### 부팅 시 설정 동기화 요청
```c
// MQTT 연결 완료 후 자동 호출
mqtt_handler_request_config_sync();

// 발행 토픽
"user/{user_id}/device/{device_id}/config/sync"

// 메시지 내용
{
    "device_id": "ESP32_ABCD1234",
    "user_id": "550e8400-...",
    "current_version": "2.1.0",
    "config_hash": "a1b2c3d4",
    "timestamp": 1706102400
}
```

### 원격 명령 타입
```c
typedef enum {
    MQTT_CMD_UPDATE_CONFIG   = 0x01,  // 설정 업데이트
    MQTT_CMD_RESTART         = 0x02,  // 재시작
    MQTT_CMD_REQUEST_STATUS  = 0x03,  // 상태 요청
    MQTT_CMD_START_MONITOR   = 0x04,  // 모니터링 시작
    MQTT_CMD_STOP_MONITOR    = 0x05,  // 모니터링 중지
    MQTT_CMD_FACTORY_RESET   = 0x06,  // 공장 초기화
} mqtt_cmd_type_t;
```

### 명령 응답
```c
// 발행 토픽
"user/{user_id}/device/{device_id}/response"

// 응답 형식
{
    "request_id": "uuid",
    "success": true,
    "timestamp": 1706102400,
    "message": "Command executed"
}
```

---

## MQTT 메시지 형식 변경

### Data 메시지 (v2.1)
```json
{
    "device_id": "ESP32_ABCD1234",
    "user_id": "550e8400-...",           // ★ 추가
    "timestamp": 1706102400,
    "sequence": 12345,
    "protocol": "custom",
    "crc_valid": true,                    // ★ 추가
    "schema_version": "2.1.0",            // ★ 추가
    "raw_hex": "02A1B2C3D403",
    "fields": {
        "Temperature": { "value": 25.5, "type": "FLOAT32", "raw": 1079042048 }
    }
}
```

### Status 메시지 (v2.1)
```json
{
    "device_id": "ESP32_ABCD1234",
    "user_id": "550e8400-...",           // ★ 추가
    "timestamp": 1706102400,
    "wifi_connected": true,
    "wifi_rssi": -65,
    "mqtt_connected": true,
    "uart_active": true,
    "uptime_seconds": 86400,
    "rx_count": 1000,
    "tx_count": 1000,
    "error_count": 5,
    "free_heap": 180000,                  // ★ 추가
    "config_hash": "a1b2c3d4",            // ★ 추가
    "firmware_version": "2.1.0",
    "schema_version": "2.1.0"             // ★ 추가
}
```

---

## 컴파일 및 테스트

### 빌드 명령
```bash
cd esp32_project
idf.py build
```

### 플래시 명령
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### 테스트 체크리스트
- [ ] BLE로 MQTT 설정 전송 (user_id, device_id, base_topic 필수)
- [ ] MQTT 연결 후 토픽 형식 확인: `user/{userId}/device/{deviceId}/...`
- [ ] config/sync 토픽 발행 확인
- [ ] cmd 토픽 원격 명령 수신 확인
- [ ] JWT 토큰 인증 테스트 (use_jwt=true)

---

## 하위 호환성 주의사항

⚠️ **Breaking Changes**:
1. Flutter 앱에서 MQTT 설정 패킷 형식 변경 필요
2. `user_id`, `device_id`, `base_topic` 없으면 설정 거부됨
3. password 길이 필드가 1바이트 → 2바이트로 변경됨

기존 v1.0 앱과 호환되지 않으므로 Flutter 앱도 반드시 v2.1로 업데이트해야 합니다.

---

## 다음 단계

1. **Flutter 앱 수정**: BLE 패킷 빌더 업데이트
2. **Backend 수정**: API 응답에 v2.1 필드 포함
3. **E2E 테스트**: 전체 흐름 검증
