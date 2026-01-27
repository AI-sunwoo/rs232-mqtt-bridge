/**
 * @file mqtt_handler.h
 * @brief MQTT Client Handler
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_event_cb_t)(bool connected);

/**
 * @brief MQTT 초기화
 */
esp_err_t mqtt_handler_init(void);

/**
 * @brief MQTT 연결 시작
 */
esp_err_t mqtt_handler_start(const mqtt_config_data_t *config);

/**
 * @brief MQTT 연결 종료
 */
void mqtt_handler_stop(void);

/**
 * @brief MQTT 연결 상태 확인
 */
bool mqtt_handler_is_connected(void);

/**
 * @brief 파싱된 데이터 발행 (Section 8.2)
 */
esp_err_t mqtt_handler_publish_data(const char *device_id,
                                    const parsed_field_t *fields,
                                    uint8_t field_count,
                                    const uint8_t *raw_data,
                                    size_t raw_len,
                                    uint16_t sequence);

/**
 * @brief 장치 상태 발행 (Section 8.3)
 */
esp_err_t mqtt_handler_publish_status(const char *device_id,
                                       const device_status_t *status);

/**
 * @brief 전송 카운트 반환
 */
uint32_t mqtt_handler_get_tx_count(void);

/**
 * @brief 이벤트 콜백 설정
 */
void mqtt_handler_set_callback(mqtt_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // MQTT_HANDLER_H
