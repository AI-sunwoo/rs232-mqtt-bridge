/**
 * @file mqtt_handler.h
 * @brief MQTT Client Handler Interface
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * 통합 인터페이스 정의서 v3.0 기반
 * S1-1: Legacy 토픽 제거
 * S1-2: QoS 기본값 1
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "esp_err.h"
#include "protocol_def.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT connection event callback
 * @param connected true if connected, false if disconnected
 */
typedef void (*mqtt_event_cb_t)(bool connected);

/**
 * @brief MQTT remote command callback (P0-3)
 * @param cmd Parsed command structure
 * @param payload cJSON payload object (may be NULL)
 */
typedef void (*mqtt_cmd_cb_t)(const mqtt_remote_command_t *cmd, cJSON *payload);

/**
 * @brief Initialize MQTT handler
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_init(void);

/**
 * @brief Start MQTT client with given configuration
 * @param config MQTT configuration (v2.1 with user_id, device_id, base_topic)
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_start(const mqtt_config_data_t *config);

/**
 * @brief Stop MQTT client
 */
void mqtt_handler_stop(void);

/**
 * @brief Check if MQTT is connected
 * @return true if connected
 */
bool mqtt_handler_is_connected(void);

/**
 * @brief Publish parsed data to MQTT (v2.1 enhanced)
 * @param device_id Device identifier
 * @param fields Array of parsed fields
 * @param field_count Number of fields
 * @param raw_data Raw frame data
 * @param raw_len Raw data length
 * @param sequence Sequence number
 * @param crc_valid CRC validation result (v2.1)
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_publish_data(const char *device_id,
                                    const parsed_field_t *fields,
                                    uint8_t field_count,
                                    const uint8_t *raw_data,
                                    size_t raw_len,
                                    uint16_t sequence,
                                    bool crc_valid);

/**
 * @brief Publish device status to MQTT (v2.1 enhanced)
 * @param device_id Device identifier
 * @param status Device status structure
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_publish_status(const char *device_id,
                                       const device_status_t *status);

/**
 * @brief Request config sync from server (P0-3)
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_request_config_sync(void);

/**
 * @brief Send command response (P0-3)
 * @param request_id Original request ID
 * @param success Command result
 * @param message Optional message
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_send_command_response(const char *request_id, 
                                              bool success, 
                                              const char *message);

/**
 * @brief Get transmitted message count
 * @return Number of messages sent
 */
uint32_t mqtt_handler_get_tx_count(void);

/**
 * @brief Set connection event callback
 * @param cb Callback function
 */
void mqtt_handler_set_callback(mqtt_event_cb_t cb);

/**
 * @brief Set remote command callback (P0-3)
 * @param cb Callback function
 */
void mqtt_handler_set_cmd_callback(mqtt_cmd_cb_t cb);

/**
 * @brief Get current MQTT configuration
 * @return Pointer to configuration (read-only)
 */
const mqtt_config_data_t* mqtt_handler_get_config(void);

/**
 * @brief Upload current device config to server (for sync after BLE config)
 * @param protocol_config Protocol configuration (custom protocol settings)
 * @param fields Field definitions array
 * @param field_count Number of fields
 * @param uart_config UART configuration
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_upload_config(const protocol_config_data_t *protocol_config,
                                      const data_definition_t *data_def,
                                      const uart_config_data_t *uart_config);

#ifdef __cplusplus
}
#endif

#endif // MQTT_HANDLER_H
