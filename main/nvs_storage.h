/**
 * @file nvs_storage.h
 * @brief NVS Storage Management
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NVS 초기화
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief WiFi 설정 저장
 */
esp_err_t nvs_save_wifi_config(const wifi_config_data_t *config);

/**
 * @brief WiFi 설정 로드
 */
esp_err_t nvs_load_wifi_config(wifi_config_data_t *config);

/**
 * @brief MQTT 설정 저장
 */
esp_err_t nvs_save_mqtt_config(const mqtt_config_data_t *config);

/**
 * @brief MQTT 설정 로드
 */
esp_err_t nvs_load_mqtt_config(mqtt_config_data_t *config);

/**
 * @brief UART 설정 저장
 */
esp_err_t nvs_save_uart_config(const uart_config_data_t *config);

/**
 * @brief UART 설정 로드
 */
esp_err_t nvs_load_uart_config(uart_config_data_t *config);

/**
 * @brief 프로토콜 설정 저장
 */
esp_err_t nvs_save_protocol_config(const protocol_config_data_t *config);

/**
 * @brief 프로토콜 설정 로드
 */
esp_err_t nvs_load_protocol_config(protocol_config_data_t *config);

/**
 * @brief 데이터 정의 저장
 */
esp_err_t nvs_save_data_definition(const data_definition_t *def);

/**
 * @brief 데이터 정의 로드
 */
esp_err_t nvs_load_data_definition(data_definition_t *def);

/**
 * @brief 공장 초기화
 */
esp_err_t nvs_reset_to_defaults(void);

/**
 * @brief 설정 완료 여부 확인
 */
bool nvs_is_configured(void);

#ifdef __cplusplus
}
#endif

#endif // NVS_STORAGE_H
