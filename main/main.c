/**
 * @file main.c
 * @brief RS232 to MQTT Bridge - Main Application
 * @version 1.0
 * @date 2026-01-24
 * 
 * ESP32-S3 based RS232 to MQTT bridge with BLE configuration
 * FreeRTOS based implementation
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "protocol_def.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "uart_handler.h"
#include "ble_service.h"
#include "data_parser.h"
#include "ota_handler.h"

static const char *TAG = "MAIN";

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
static device_status_t g_device_status = {0};
static wifi_config_data_t g_wifi_config = {0};
static mqtt_config_data_t g_mqtt_config = {0};
static uart_config_data_t g_uart_config = {0};
static protocol_config_data_t g_protocol_config = {0};
static data_definition_t g_data_definition = {0};

static char g_device_id[32] = {0};
static uint16_t g_sequence = 0;
static bool g_monitoring_enabled = false;
static uint32_t g_start_time = 0;

static SemaphoreHandle_t g_config_mutex = NULL;
static QueueHandle_t g_frame_queue = NULL;

// Frame queue item
typedef struct {
    uint8_t data[FRAME_BUF_SIZE];
    size_t length;
} frame_item_t;

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
static void generate_device_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_device_id, sizeof(g_device_id), "ESP32_%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device ID: %s", g_device_id);
}

static void update_status(void)
{
    if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_device_status.wifi_status = wifi_manager_is_connected() ? 1 : 0;
        g_device_status.mqtt_status = mqtt_handler_is_connected() ? 1 : 0;
        g_device_status.uart_status = uart_handler_is_receiving() ? 1 : 0;
        g_device_status.config_status = nvs_is_configured() ? 1 : 0;
        g_device_status.rssi = wifi_manager_get_rssi();
        g_device_status.uptime = (esp_timer_get_time() / 1000000) - g_start_time;
        g_device_status.rx_count = uart_handler_get_rx_count();
        g_device_status.tx_count = mqtt_handler_get_tx_count();
        g_device_status.error_count = uart_handler_get_error_count();
        g_device_status.firmware_version = FIRMWARE_VERSION;
        xSemaphoreGive(g_config_mutex);
    }
}

/*******************************************************************************
 * Configuration Parsers
 ******************************************************************************/
static esp_err_t parse_wifi_config(const uint8_t *data, uint16_t len)
{
    if (len < 2) return ESP_ERR_INVALID_ARG;

    wifi_config_data_t config = {0};
    uint8_t offset = 0;

    // SSID length
    uint8_t ssid_len = data[offset++];
    if (ssid_len > WIFI_SSID_MAX_LEN || offset + ssid_len > len) {
        return ESP_ERR_INVALID_ARG;
    }

    // SSID
    memcpy(config.ssid, &data[offset], ssid_len);
    config.ssid[ssid_len] = '\0';
    offset += ssid_len;

    // Password length
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t pwd_len = data[offset++];
    if (pwd_len > WIFI_PASSWORD_MAX_LEN || offset + pwd_len > len) {
        return ESP_ERR_INVALID_ARG;
    }

    // Password
    memcpy(config.password, &data[offset], pwd_len);
    config.password[pwd_len] = '\0';

    ESP_LOGI(TAG, "WiFi config parsed: SSID=%s", config.ssid);

    // Save to global and NVS
    memcpy(&g_wifi_config, &config, sizeof(wifi_config_data_t));
    return nvs_save_wifi_config(&config);
}

static esp_err_t parse_mqtt_config(const uint8_t *data, uint16_t len)
{
    if (len < 4) return ESP_ERR_INVALID_ARG;

    mqtt_config_data_t config = {0};
    uint16_t offset = 0;

    // Broker
    uint8_t broker_len = data[offset++];
    if (broker_len > MQTT_BROKER_MAX_LEN || offset + broker_len > len) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config.broker, &data[offset], broker_len);
    offset += broker_len;

    // Port (Little Endian)
    if (offset + 2 > len) return ESP_ERR_INVALID_ARG;
    config.port = data[offset] | (data[offset + 1] << 8);
    offset += 2;

    // Username
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t user_len = data[offset++];
    if (offset + user_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config.username, &data[offset], user_len);
    offset += user_len;

    // Password
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t pwd_len = data[offset++];
    if (offset + pwd_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config.password, &data[offset], pwd_len);
    offset += pwd_len;

    // Client ID
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t cid_len = data[offset++];
    if (offset + cid_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config.client_id, &data[offset], cid_len);
    offset += cid_len;

    // If no client ID provided, use device ID
    if (strlen(config.client_id) == 0) {
        strcpy(config.client_id, g_device_id);
    }

    // Topic
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t topic_len = data[offset++];
    if (offset + topic_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config.topic, &data[offset], topic_len);
    offset += topic_len;

    // QoS
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    config.qos = data[offset++];
    if (config.qos > 2) config.qos = 1;

    // TLS
    if (offset < len) {
        config.use_tls = (data[offset] != 0);
    }

    ESP_LOGI(TAG, "MQTT config parsed: broker=%s:%d, topic=%s",
             config.broker, config.port, config.topic);

    memcpy(&g_mqtt_config, &config, sizeof(mqtt_config_data_t));
    return nvs_save_mqtt_config(&config);
}

static esp_err_t parse_uart_config(const uint8_t *data, uint16_t len)
{
    if (len < 8) return ESP_ERR_INVALID_ARG;

    uart_config_data_t config = {0};

    // Baudrate (Little Endian)
    config.baudrate = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);

    // Data bits
    config.data_bits = data[4];
    if (config.data_bits != 7 && config.data_bits != 8) {
        config.data_bits = 8;
    }

    // Parity
    config.parity = data[5];
    if (config.parity > 2) config.parity = 0;

    // Stop bits
    config.stop_bits = data[6];
    if (config.stop_bits != 1 && config.stop_bits != 2) {
        config.stop_bits = 1;
    }

    // Flow control
    config.flow_control = data[7];
    if (config.flow_control > 2) config.flow_control = 0;

    ESP_LOGI(TAG, "UART config parsed: %lu-%d-%d-%d",
             (unsigned long)config.baudrate, config.data_bits, 
             config.parity, config.stop_bits);

    memcpy(&g_uart_config, &config, sizeof(uart_config_data_t));
    return nvs_save_uart_config(&config);
}

static esp_err_t parse_protocol_config(const uint8_t *data, uint16_t len)
{
    if (len < 3) return ESP_ERR_INVALID_ARG;

    protocol_config_data_t config = {0};
    config.type = (protocol_type_t)data[0];

    uint16_t config_len = data[1] | (data[2] << 8);
    const uint8_t *config_data = &data[3];

    if (3 + config_len > len) return ESP_ERR_INVALID_ARG;

    switch (config.type) {
        case PROTOCOL_CUSTOM:
            if (config_len >= sizeof(custom_protocol_config_t)) {
                memcpy(&config.config.custom, config_data, sizeof(custom_protocol_config_t));
            }
            break;

        case PROTOCOL_MODBUS_RTU:
        case PROTOCOL_MODBUS_ASCII:
            if (config_len >= sizeof(modbus_rtu_config_t)) {
                memcpy(&config.config.modbus_rtu, config_data, sizeof(modbus_rtu_config_t));
            }
            break;

        case PROTOCOL_NMEA_0183:
            if (config_len >= 3) {
                config.config.nmea.sentence_filter_count = config_data[0];
                // Parse sentence filters...
                config.config.nmea.validate_checksum = true;
            }
            break;

        case PROTOCOL_IEC_60870_101:
        case PROTOCOL_IEC_60870_104:
            if (config_len >= sizeof(iec60870_config_t)) {
                memcpy(&config.config.iec60870, config_data, sizeof(iec60870_config_t));
            }
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Protocol config parsed: type=%d", config.type);

    memcpy(&g_protocol_config, &config, sizeof(protocol_config_data_t));
    return nvs_save_protocol_config(&config);
}

static esp_err_t parse_data_definition(const uint8_t *data, uint16_t len)
{
    if (len < 2) return ESP_ERR_INVALID_ARG;

    data_definition_t def = {0};
    def.field_count = data[0];
    def.data_offset = data[1];

    if (def.field_count > MAX_FIELD_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t offset = 2;

    // Parse field definitions (12 bytes each)
    for (uint8_t i = 0; i < def.field_count && offset + 12 <= len; i++) {
        memcpy(&def.fields[i], &data[offset], sizeof(field_definition_t));
        offset += sizeof(field_definition_t);
    }

    // Parse field names (null-separated)
    if (offset < len) {
        def.names_length = len - offset;
        if (def.names_length > MAX_FIELD_NAMES_SIZE) {
            def.names_length = MAX_FIELD_NAMES_SIZE;
        }
        memcpy(def.field_names, &data[offset], def.names_length);
    }

    ESP_LOGI(TAG, "Data definition parsed: %d fields", def.field_count);

    memcpy(&g_data_definition, &def, sizeof(data_definition_t));
    data_parser_set_definition(&def);
    return nvs_save_data_definition(&def);
}

/*******************************************************************************
 * WiFi Connect Task
 ******************************************************************************/
static void wifi_connect_task(void *arg)
{
    wifi_manager_connect(&g_wifi_config);
    vTaskDelete(NULL);
}

/*******************************************************************************
 * BLE Command Handler
 ******************************************************************************/
static void ble_command_handler(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    esp_err_t ret = ESP_OK;
    result_code_t result = RESULT_SUCCESS;

    ESP_LOGI(TAG, "Processing BLE command: 0x%02X", cmd);

    switch (cmd) {
        case CMD_SET_WIFI:
            ret = parse_wifi_config(data, len);
            if (ret == ESP_OK) {
                // Apply WiFi configuration
                xTaskCreate(wifi_connect_task, "wifi_connect", 4096, NULL, 3, NULL);
            }
            break;

        case CMD_SET_MQTT:
            ret = parse_mqtt_config(data, len);
            if (ret == ESP_OK && wifi_manager_is_connected()) {
                // Apply MQTT configuration
                mqtt_handler_stop();
                mqtt_handler_start(&g_mqtt_config);
            }
            break;

        case CMD_SET_UART:
            ret = parse_uart_config(data, len);
            if (ret == ESP_OK) {
                // Restart UART with new config
                uart_handler_stop();
                uart_handler_start(&g_uart_config, &g_protocol_config);
            }
            break;

        case CMD_SET_PROTOCOL:
            ret = parse_protocol_config(data, len);
            if (ret == ESP_OK) {
                uart_handler_update_protocol(&g_protocol_config);
            }
            break;

        case CMD_SET_DATA_DEF:
            ret = parse_data_definition(data, len);
            break;

        case CMD_GET_STATUS:
            update_status();
            ble_service_notify_status(&g_device_status);
            break;

        case CMD_SAVE_CONFIG:
            // All configs are saved automatically
            ESP_LOGI(TAG, "Configuration saved");
            break;

        case CMD_RESET_CONFIG:
            ESP_LOGW(TAG, "Factory reset requested");
            nvs_reset_to_defaults();
            esp_restart();
            break;

        case CMD_START_MONITOR:
            ESP_LOGI(TAG, "Monitoring started");
            g_monitoring_enabled = true;
            break;

        case CMD_STOP_MONITOR:
            ESP_LOGI(TAG, "Monitoring stopped");
            g_monitoring_enabled = false;
            break;

        // OTA Commands
        case CMD_OTA_CHECK:
            ESP_LOGI(TAG, "OTA version check requested");
            ret = ota_handler_check_version();
            break;

        case CMD_OTA_START:
            ESP_LOGI(TAG, "OTA update requested");
            ret = ota_handler_start();
            break;

        case CMD_OTA_ABORT:
            ESP_LOGI(TAG, "OTA abort requested");
            ota_handler_abort();
            break;

        case CMD_OTA_ROLLBACK:
            ESP_LOGI(TAG, "OTA rollback requested");
            ret = ota_handler_rollback();
            break;

        case CMD_OTA_GET_VERSION:
            {
                const ota_version_info_t *ver_info = ota_handler_get_version_info();
                // Send version info via BLE notification
                uint8_t ver_data[64];
                int ver_len = snprintf((char*)ver_data, sizeof(ver_data),
                    "{\"current\":\"%s\",\"latest\":\"%s\",\"update\":%s}",
                    ver_info->current_version,
                    ver_info->latest_version,
                    ver_info->update_available ? "true" : "false");
                ble_service_notify_data(ver_data, ver_len);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
            ret = ESP_ERR_INVALID_ARG;
            break;
    }

    // Send response
    if (ret != ESP_OK) {
        result = (ret == ESP_ERR_INVALID_ARG) ? RESULT_INVALID : RESULT_FAILED;
    }
    ble_service_send_ack(cmd, result);
}

/*******************************************************************************
 * UART Frame Handler
 ******************************************************************************/
static void uart_frame_handler(const uint8_t *data, size_t length)
{
    if (!g_frame_queue) return;

    frame_item_t item;
    if (length > FRAME_BUF_SIZE) {
        length = FRAME_BUF_SIZE;
    }
    memcpy(item.data, data, length);
    item.length = length;

    // Send to queue (don't block)
    xQueueSend(g_frame_queue, &item, 0);
}

/*******************************************************************************
 * Data Processing Task
 ******************************************************************************/
static void data_processing_task(void *arg)
{
    frame_item_t item;
    parsed_field_t fields[MAX_FIELD_COUNT];

    ESP_LOGI(TAG, "Data processing task started");

    while (1) {
        if (xQueueReceive(g_frame_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Parse the frame
            int field_count = data_parser_parse_frame(item.data, item.length,
                                                       fields, MAX_FIELD_COUNT);

            if (field_count > 0) {
                g_sequence++;

                // Send to MQTT if connected
                if (mqtt_handler_is_connected()) {
                    mqtt_handler_publish_data(g_device_id, fields, field_count,
                                              item.data, item.length, g_sequence);
                }

                // Send to BLE if monitoring enabled and connected
                if (g_monitoring_enabled && ble_service_is_connected()) {
                    // Build compact data for BLE notification
                    uint8_t ble_data[256];
                    uint16_t offset = 0;

                    // Header
                    uint32_t timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
                    memcpy(&ble_data[offset], &timestamp, 4);
                    offset += 4;
                    memcpy(&ble_data[offset], &g_sequence, 2);
                    offset += 2;
                    ble_data[offset++] = field_count;
                    ble_data[offset++] = 2;  // Format: Compact

                    // Field values (scaled, as float)
                    for (int i = 0; i < field_count && offset + 4 < sizeof(ble_data); i++) {
                        float val = (float)fields[i].scaled_value;
                        memcpy(&ble_data[offset], &val, 4);
                        offset += 4;
                    }

                    ble_service_notify_data(ble_data, offset);
                }
            }
        }
    }
}

/*******************************************************************************
 * Status Update Task
 ******************************************************************************/
static void status_task(void *arg)
{
    ESP_LOGI(TAG, "Status task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));  // Every 5 seconds

        update_status();

        // Publish status to MQTT
        if (mqtt_handler_is_connected()) {
            mqtt_handler_publish_status(g_device_id, &g_device_status);
        }

        // Send status via BLE if connected
        if (ble_service_is_connected()) {
            ble_service_notify_status(&g_device_status);
        }

        // Log status
        ESP_LOGI(TAG, "Status: WiFi=%d, MQTT=%d, UART=%d, RX=%lu, TX=%lu, Err=%lu",
                 g_device_status.wifi_status,
                 g_device_status.mqtt_status,
                 g_device_status.uart_status,
                 (unsigned long)g_device_status.rx_count,
                 (unsigned long)g_device_status.tx_count,
                 (unsigned long)g_device_status.error_count);
    }
}

/*******************************************************************************
 * WiFi/MQTT Event Handlers
 ******************************************************************************/
static void wifi_event_handler(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "WiFi connected, starting MQTT...");
        if (strlen(g_mqtt_config.broker) > 0) {
            mqtt_handler_start(&g_mqtt_config);
        }
    } else {
        ESP_LOGW(TAG, "WiFi disconnected");
        mqtt_handler_stop();
    }
}

/*******************************************************************************
 * OTA Progress Callback
 ******************************************************************************/
static void ota_progress_callback(ota_state_t state, uint8_t progress, ota_error_t error)
{
    // Send OTA progress to BLE app
    uint8_t progress_data[64];  // Increased buffer size for JSON strings
    int len = 0;
    
    switch (state) {
        case OTA_STATE_CHECKING:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"check\",\"p\":%d}", progress);
            break;
        case OTA_STATE_DOWNLOADING:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"dl\",\"p\":%d}", progress);
            break;
        case OTA_STATE_VERIFYING:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"verify\",\"p\":%d}", progress);
            break;
        case OTA_STATE_APPLYING:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"apply\",\"p\":%d}", progress);
            break;
        case OTA_STATE_SUCCESS:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"ok\",\"p\":100}");
            ESP_LOGI(TAG, "OTA Success - Rebooting...");
            break;
        case OTA_STATE_FAILED:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"fail\",\"err\":%d}", (int)error);
            ESP_LOGE(TAG, "OTA Failed with error: %d", (int)error);
            break;
        case OTA_STATE_NO_UPDATE:
            len = snprintf((char*)progress_data, sizeof(progress_data),
                          "{\"st\":\"latest\",\"p\":100}");
            break;
        default:
            return;
    }
    
    if (len > 0 && ble_service_is_connected()) {
        ble_service_notify_data(progress_data, len);
    }
}

static void mqtt_event_handler(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "MQTT connected");
        // Publish initial status
        update_status();
        mqtt_handler_publish_status(g_device_id, &g_device_status);
    } else {
        ESP_LOGW(TAG, "MQTT disconnected");
    }
}

/*******************************************************************************
 * Main Application
 ******************************************************************************/
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "RS232 to MQTT Bridge v%d.%d.%d",
             (FIRMWARE_VERSION >> 24) & 0xFF,
             (FIRMWARE_VERSION >> 16) & 0xFF,
             (FIRMWARE_VERSION >> 8) & 0xFF);
    ESP_LOGI(TAG, "========================================");

    // Record start time
    g_start_time = esp_timer_get_time() / 1000000;

    // Create mutex
    g_config_mutex = xSemaphoreCreateMutex();
    if (!g_config_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Create frame queue
    g_frame_queue = xQueueCreate(UART_RX_QUEUE_SIZE, sizeof(frame_item_t));
    if (!g_frame_queue) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        return;
    }

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_storage_init());

    // Generate device ID
    generate_device_id();

    // Load saved configurations
    nvs_load_wifi_config(&g_wifi_config);
    nvs_load_mqtt_config(&g_mqtt_config);
    nvs_load_uart_config(&g_uart_config);
    nvs_load_protocol_config(&g_protocol_config);
    nvs_load_data_definition(&g_data_definition);

    // Initialize data parser
    data_parser_init();
    if (g_data_definition.field_count > 0) {
        data_parser_set_definition(&g_data_definition);
    }

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_manager_init());
    wifi_manager_set_callback(wifi_event_handler);

    // Initialize MQTT
    ESP_ERROR_CHECK(mqtt_handler_init());
    mqtt_handler_set_callback(mqtt_event_handler);

    // Initialize UART
    ESP_ERROR_CHECK(uart_handler_init());
    uart_handler_set_callback(uart_frame_handler);

    // Initialize OTA
    ESP_ERROR_CHECK(ota_handler_init());
    ota_handler_set_callback(ota_progress_callback);

    // Initialize BLE
    ESP_ERROR_CHECK(ble_service_init(DEVICE_NAME));
    ble_service_set_callback(ble_command_handler);
    ble_service_start();

    // Create data processing task BEFORE starting UART
    xTaskCreate(data_processing_task, "data_proc", TASK_STACK_PARSER,
                NULL, TASK_PRIORITY_PARSER, NULL);

    // Small delay to ensure task is ready
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start UART with default or saved config
    uart_handler_start(&g_uart_config, &g_protocol_config);

    // Connect to WiFi if configured
    if (strlen(g_wifi_config.ssid) > 0) {
        ESP_LOGI(TAG, "Connecting to saved WiFi: %s", g_wifi_config.ssid);
        wifi_manager_connect(&g_wifi_config);
    } else {
        ESP_LOGI(TAG, "No WiFi configured, waiting for BLE configuration...");
    }

    // Create status task
    xTaskCreate(status_task, "status", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "System initialized successfully");
    ESP_LOGI(TAG, "BLE Device Name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Waiting for connections...");
}
