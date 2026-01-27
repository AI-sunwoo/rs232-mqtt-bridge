/**
 * @file cmd_handler.h
 * @brief BLE Command Handler Header
 */

#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process BLE command
 * @param cmd Command code
 * @param data Payload data
 * @param len Payload length
 */
void cmd_handler_process(cmd_code_t cmd, const uint8_t *data, uint16_t len);

/**
 * @brief Parse WiFi configuration from payload
 */
esp_err_t cmd_parse_wifi_config(const uint8_t *data, uint16_t len, wifi_config_data_t *config);

/**
 * @brief Parse MQTT configuration from payload
 */
esp_err_t cmd_parse_mqtt_config(const uint8_t *data, uint16_t len, mqtt_config_data_t *config);

/**
 * @brief Parse UART configuration from payload
 */
esp_err_t cmd_parse_uart_config(const uint8_t *data, uint16_t len, uart_config_data_t *config);

/**
 * @brief Parse protocol configuration from payload
 */
esp_err_t cmd_parse_protocol_config(const uint8_t *data, uint16_t len, protocol_config_data_t *config);

/**
 * @brief Parse data definition from payload
 */
esp_err_t cmd_parse_data_definition(const uint8_t *data, uint16_t len, data_definition_t *def);

#ifdef __cplusplus
}
#endif

#endif // CMD_HANDLER_H
