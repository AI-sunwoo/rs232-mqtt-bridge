/**
 * @file ble_service.h
 * @brief BLE GATT Service for Configuration
 * @version 3.0.0
 * @date 2026-02-04
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_cmd_cb_t)(uint8_t cmd, const uint8_t *data, uint16_t len);

/**
 * @brief BLE 서비스 초기화
 */
esp_err_t ble_service_init(const char *device_name);

/**
 * @brief BLE Advertising 시작
 */
void ble_service_start(void);

/**
 * @brief BLE Advertising 중지
 */
void ble_service_stop(void);

/**
 * @brief BLE 연결 상태 확인
 */
bool ble_service_is_connected(void);

/**
 * @brief BLE 암호화 상태 확인 (페어링 완료 여부)
 */
bool ble_service_is_encrypted(void);

/**
 * @brief 상태 알림 전송 (Section 7.2)
 */
esp_err_t ble_service_notify_status(const device_status_t *status);

/**
 * @brief 파싱된 데이터 알림 전송 (Section 7.3)
 */
esp_err_t ble_service_notify_parsed_data(const uint8_t *data, uint16_t len);

// Alias for backward compatibility
#define ble_service_notify_data ble_service_notify_parsed_data

/**
 * @brief ACK 응답 전송 (Section 7.1)
 */
esp_err_t ble_service_send_ack(uint8_t cmd, uint8_t result);

/**
 * @brief 명령 콜백 설정
 */
void ble_service_set_callback(ble_cmd_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // BLE_SERVICE_H
