/**
 * @file main.c
 * @brief RS232 to MQTT Bridge - Main Application
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * ESP32-S3 based RS232 to MQTT bridge with BLE configuration
 * FreeRTOS based implementation
 * 통합 인터페이스 정의서 v3.0 적용
 * 
 * v3.0 변경사항:
 * - S2-3: 중복 파서 제거 (main.c → cmd_handler.c 위임)
 *         parse_wifi/mqtt/uart/protocol/data_definition 제거 (~535줄)
 *         ble_command_handler → OTA만 직접 처리, 나머지 cmd_handler_process 위임
 * - S1-1: Legacy MQTT 토픽 제거 (mqtt_handler.c에서 처리)
 * - S1-2: QoS 0 → 1 변경 (protocol_def.h DEFAULT_MQTT_QOS)
 * - WiFi 지수 백오프 재연결 (wifi_manager.c에서 처리)
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
#include "cmd_handler.h"
#include "ota_handler.h"

static const char *TAG = "MAIN";

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
// Global configurations - non-static for extern access from cmd_handler
device_status_t g_device_status = {0};
wifi_config_data_t g_wifi_config = {0};
mqtt_config_data_t g_mqtt_config = {0};
uart_config_data_t g_uart_config = {0};
protocol_config_data_t g_protocol_config = {0};
data_definition_t g_data_definition = {0};

char g_device_id[32] = {0};  // Non-static for extern access from cmd_handler
static uint16_t g_sequence = 0;
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
        g_device_status.free_heap = esp_get_free_heap_size();
        nvs_calculate_config_hash(g_device_status.config_hash, sizeof(g_device_status.config_hash));
        
        xSemaphoreGive(g_config_mutex);
    }
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
 * 
 * 특허 2.4절 "실시간 검증부" 구현:
 * - 설정 전송 직후 파싱 결과를 BLE로 실시간 전송
 * - Raw Data + 파싱된 물리값 + CRC 검증 결과 포함
 ******************************************************************************/
static void data_processing_task(void *arg)
{
    frame_item_t item;
    parsed_field_t fields[MAX_FIELD_COUNT];

    ESP_LOGI(TAG, "Data processing task started");

    while (1) {
        if (xQueueReceive(g_frame_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool crc_valid = true;
            int field_count = data_parser_parse_frame(item.data, item.length,
                                                       fields, MAX_FIELD_COUNT);

            if (field_count > 0) {
                g_sequence++;

                // Send to MQTT if connected
                if (mqtt_handler_is_connected()) {
                    mqtt_handler_publish_data(g_device_id, fields, field_count,
                                              item.data, item.length, g_sequence, crc_valid);
                }

                // 특허 실시간 검증부: BLE 연결 시 항상 파싱 결과 전송
                if (ble_service_is_connected()) {
                    uint8_t ble_data[512];
                    uint16_t offset = 0;

                    // Packet header
                    ble_data[offset++] = PACKET_STX;
                    ble_data[offset++] = RSP_DATA;
                    uint16_t len_offset = offset;
                    offset += 2;  // Length placeholder

                    // Header: timestamp(4) + sequence(2) + field_count(1) + format(1)
                    uint32_t timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
                    memcpy(&ble_data[offset], &timestamp, 4);
                    offset += 4;
                    memcpy(&ble_data[offset], &g_sequence, 2);
                    offset += 2;
                    ble_data[offset++] = field_count;
                    ble_data[offset++] = 1;  // Format: JSON-like with raw

                    // Raw data hex (최대 32 bytes)
                    uint8_t raw_len = (item.length > 32) ? 32 : item.length;
                    ble_data[offset++] = raw_len;
                    for (int i = 0; i < raw_len && offset + 2 < sizeof(ble_data) - 10; i++) {
                        uint8_t byte = item.data[i];
                        ble_data[offset++] = "0123456789ABCDEF"[byte >> 4];
                        ble_data[offset++] = "0123456789ABCDEF"[byte & 0x0F];
                    }

                    // CRC verification result
                    ble_data[offset++] = crc_valid ? 1 : 0;

                    // Field values with names
                    for (int i = 0; i < field_count && offset + 40 < sizeof(ble_data) - 10; i++) {
                        uint8_t name_len = strlen(fields[i].name);
                        if (name_len > 16) name_len = 16;
                        ble_data[offset++] = name_len;
                        memcpy(&ble_data[offset], fields[i].name, name_len);
                        offset += name_len;

                        float val = (float)fields[i].scaled_value;
                        memcpy(&ble_data[offset], &val, 4);
                        offset += 4;

                        ble_data[offset++] = fields[i].type;
                    }

                    // Fill in length
                    uint16_t payload_len = offset - 4;
                    ble_data[len_offset] = payload_len & 0xFF;
                    ble_data[len_offset + 1] = (payload_len >> 8) & 0xFF;

                    // CRC and ETX
                    uint8_t crc = 0;
                    for (int i = 1; i < offset; i++) {
                        crc ^= ble_data[i];
                    }
                    ble_data[offset++] = crc;
                    ble_data[offset++] = PACKET_ETX;

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
        vTaskDelay(pdMS_TO_TICKS(1000));  // Every 1 second (changed from 5s)

        update_status();

        if (mqtt_handler_is_connected()) {
            mqtt_handler_publish_status(g_device_id, &g_device_status);
        }

        if (ble_service_is_connected()) {
            ble_service_notify_status(&g_device_status);
        }

        ESP_LOGI(TAG, "Status: WiFi=%d MQTT=%d UART=%d RX=%lu TX=%lu Err=%lu Heap=%lu",
                 g_device_status.wifi_status,
                 g_device_status.mqtt_status,
                 g_device_status.uart_status,
                 (unsigned long)g_device_status.rx_count,
                 (unsigned long)g_device_status.tx_count,
                 (unsigned long)g_device_status.error_count,
                 (unsigned long)g_device_status.free_heap);
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

static void mqtt_event_handler(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "MQTT connected");
        update_status();
        mqtt_handler_publish_status(g_device_id, &g_device_status);

        // OTA 후 첫 부팅: MQTT 연결 성공 = 시스템 정상 → 펌웨어 유효 마킹
        static bool s_ota_validated = false;
        if (!s_ota_validated) {
            s_ota_validated = true;
            if (ota_handler_mark_valid() == ESP_OK) {
                ESP_LOGI(TAG, "New firmware validated after successful MQTT connection");
            }
        }
    } else {
        ESP_LOGW(TAG, "MQTT disconnected");
    }
}

/*******************************************************************************
 * OTA Progress Callback
 ******************************************************************************/
static void ota_progress_callback(ota_state_t state, uint8_t progress, ota_error_t error)
{
    uint8_t progress_data[64];
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
            ESP_LOGE(TAG, "OTA Failed: %d", (int)error);
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

/*******************************************************************************
 * BLE Command Handler
 * v3.0 S2-3: OTA만 직접 처리, 나머지 모든 명령은 cmd_handler_process() 위임
 ******************************************************************************/
static void ble_command_handler(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "BLE cmd: 0x%02X (len=%d)", cmd, len);

    // OTA commands handled locally (require ota_handler direct access)
    switch (cmd) {
        case CMD_OTA_CHECK: {
            esp_err_t err = ota_handler_check_version();
            ble_service_send_ack(cmd, (err == ESP_OK) ? RESULT_SUCCESS : RESULT_FAILED);
            return;
        }
        case CMD_OTA_START: {
            esp_err_t err = ota_handler_start();
            ble_service_send_ack(cmd, (err == ESP_OK) ? RESULT_SUCCESS : RESULT_FAILED);
            return;
        }
        case CMD_OTA_ABORT:
            ota_handler_abort();
            ble_service_send_ack(cmd, RESULT_SUCCESS);
            return;
        case CMD_OTA_ROLLBACK: {
            esp_err_t err = ota_handler_rollback();
            ble_service_send_ack(cmd, (err == ESP_OK) ? RESULT_SUCCESS : RESULT_FAILED);
            return;
        }
        case CMD_OTA_GET_VERSION: {
            const ota_version_info_t *vi = ota_handler_get_version_info();
            uint8_t vd[64];
            int vl = snprintf((char*)vd, sizeof(vd),
                "{\"current\":\"%s\",\"latest\":\"%s\",\"update\":%s}",
                vi->current_version, vi->latest_version,
                vi->update_available ? "true" : "false");
            ble_service_notify_data(vd, vl);
            ble_service_send_ack(cmd, RESULT_SUCCESS);
            return;
        }
        default:
            break;
    }

    // All other commands → cmd_handler.c (S2-3: single source of truth)
    cmd_handler_process((cmd_code_t)cmd, data, len);
}

/*******************************************************************************
 * Main Application
 ******************************************************************************/
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "RS232-MQTT Bridge v%d.%d.%d (Schema %s)",
             (FIRMWARE_VERSION >> 24) & 0xFF,
             (FIRMWARE_VERSION >> 16) & 0xFF,
             (FIRMWARE_VERSION >> 8) & 0xFF,
             SCHEMA_VERSION_STRING);
    ESP_LOGI(TAG, "========================================");

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

    // Initialize subsystems
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nvs_storage_init());
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
    mqtt_handler_set_cmd_callback(cmd_handler_process_remote);

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

    // Create data processing task
    xTaskCreate(data_processing_task, "data_proc", TASK_STACK_PARSER,
                NULL, TASK_PRIORITY_PARSER, NULL);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start UART
    uart_handler_start(&g_uart_config, &g_protocol_config);

    // Connect to WiFi if configured
    if (strlen(g_wifi_config.ssid) > 0) {
        ESP_LOGI(TAG, "Connecting to saved WiFi: %s", g_wifi_config.ssid);
        wifi_manager_connect(&g_wifi_config);
    } else {
        ESP_LOGI(TAG, "No WiFi configured, waiting for BLE...");
    }

    // Create status task
    xTaskCreate(status_task, "status", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "System initialized - BLE: %s", DEVICE_NAME);
}
