/**
 * @file cmd_handler.h
 * @brief BLE Command Handler Interface
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * v3.0 S2-3: 유일한 명령 파싱 소스 (main.c 중복 제거)
 */

#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include "esp_err.h"
#include "protocol_def.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse WiFi configuration from BLE packet
 * @param data Raw packet data
 * @param len Data length
 * @param config Output configuration
 * @return ESP_OK on success
 */
esp_err_t cmd_parse_wifi_config(const uint8_t *data, uint16_t len, wifi_config_data_t *config);

/**
 * @brief Parse MQTT configuration from BLE packet (v2.1)
 * 
 * v2.1 패킷에는 다음 필수 필드가 포함되어야 함:
 * - user_id (P0-1)
 * - device_id (P0-1)
 * - base_topic (P0-1)
 * - use_jwt (P0-2)
 * 
 * @param data Raw packet data
 * @param len Data length
 * @param config Output configuration
 * @return ESP_OK on success
 */
esp_err_t cmd_parse_mqtt_config(const uint8_t *data, uint16_t len, mqtt_config_data_t *config);

/**
 * @brief Parse UART configuration from BLE packet
 * @param data Raw packet data
 * @param len Data length
 * @param config Output configuration
 * @return ESP_OK on success
 */
esp_err_t cmd_parse_uart_config(const uint8_t *data, uint16_t len, uart_config_data_t *config);

/**
 * @brief Parse protocol configuration from BLE packet
 * @param data Raw packet data
 * @param len Data length
 * @param config Output configuration
 * @return ESP_OK on success
 */
esp_err_t cmd_parse_protocol_config(const uint8_t *data, uint16_t len, protocol_config_data_t *config);

/**
 * @brief Parse data field definition from BLE packet
 * @param data Raw packet data
 * @param len Data length
 * @param def Output definition
 * @return ESP_OK on success
 */
esp_err_t cmd_parse_data_definition(const uint8_t *data, uint16_t len, data_definition_t *def);

/**
 * @brief Process BLE command
 * @param cmd Command code
 * @param data Payload data
 * @param len Payload length
 */
void cmd_handler_process(cmd_code_t cmd, const uint8_t *data, uint16_t len);

/**
 * @brief Process remote MQTT command (P0-3)
 * @param cmd Command structure
 * @param payload JSON payload (may be NULL)
 */
void cmd_handler_process_remote(const mqtt_remote_command_t *cmd, cJSON *payload);

#ifdef __cplusplus
}
#endif

#endif // CMD_HANDLER_H
