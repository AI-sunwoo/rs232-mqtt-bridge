/**
 * @file mqtt_handler.c
 * @brief MQTT Client Handler Implementation
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * 통합 인터페이스 정의서 v3.0 기반
 * S1-1: Legacy 토픽 제거 (rs232/, device/ 폴백 제거)
 * S1-2: QoS 0 → 1 변경
 * P0-3: 원격 명령 처리, 설정 동기화 유지
 */

#include "mqtt_handler.h"
#include "mqtt_client.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static mqtt_event_cb_t s_event_callback = NULL;
static mqtt_cmd_cb_t s_cmd_callback = NULL;         // v2.1: 원격 명령 콜백
static mqtt_config_data_t s_config = {0};
static uint32_t s_tx_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

// Forward declarations
static void handle_remote_command(const char *topic, const char *payload, int len);
static void handle_config_download(const char *payload, int len);

/*******************************************************************************
 * MQTT Event Handler
 ******************************************************************************/
static void mqtt_event_handler(void *args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            s_connected = true;
            
            // v3.0: user_id + device_id 필수 (legacy 토픽 제거 - S1-1)
            if (strlen(s_config.user_id) > 0 && strlen(s_config.device_id) > 0) {
                char topic[256];
                
                // 1. cmd 토픽 Subscribe (원격 명령 수신)
                snprintf(topic, sizeof(topic), "user/%s/device/%s/cmd", 
                         s_config.user_id, s_config.device_id);
                esp_mqtt_client_subscribe(s_client, topic, s_config.qos);
                ESP_LOGI(TAG, "Subscribed: %s", topic);
                
                // 2. config/download 토픽 Subscribe (설정 동기화)
                snprintf(topic, sizeof(topic), "user/%s/device/%s/config/download", 
                         s_config.user_id, s_config.device_id);
                esp_mqtt_client_subscribe(s_client, topic, s_config.qos);
                ESP_LOGI(TAG, "Subscribed: %s", topic);
                
                // 3. 부팅 시 설정 동기화 요청
                mqtt_handler_request_config_sync();
                
            } else {
                // v3.0: user_id/device_id 없으면 토픽 구독 불가
                ESP_LOGE(TAG, "Cannot subscribe: user_id or device_id not configured!");
                ESP_LOGE(TAG, "Please configure via BLE or QR code first");
            }
            
            if (s_event_callback) s_event_callback(true);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected");
            s_connected = false;
            if (s_event_callback) s_event_callback(false);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Data received on topic: %.*s", event->topic_len, event->topic);
            
            // 토픽에 따라 처리 분기 (P0-3)
            if (event->topic && event->data) {
                char topic_buf[256] = {0};
                char data_buf[2048] = {0};
                
                int topic_len = event->topic_len < 255 ? event->topic_len : 255;
                int data_len = event->data_len < 2047 ? event->data_len : 2047;
                
                memcpy(topic_buf, event->topic, topic_len);
                memcpy(data_buf, event->data, data_len);
                
                if (strstr(topic_buf, "/cmd")) {
                    // 원격 명령 처리
                    handle_remote_command(topic_buf, data_buf, data_len);
                } else if (strstr(topic_buf, "/config/download")) {
                    // 설정 다운로드 처리
                    handle_config_download(data_buf, data_len);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error occurred");
            if (event->error_handle) {
                ESP_LOGE(TAG, "Error type: %d", event->error_handle->error_type);
            }
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Remote Command Handler (P0-3)
 ******************************************************************************/
static void handle_remote_command(const char *topic, const char *payload, int len)
{
    ESP_LOGI(TAG, "Processing remote command");
    
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse command JSON");
        return;
    }
    
    mqtt_remote_command_t cmd = {0};
    
    // command 필드 파싱
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (command && cJSON_IsString(command)) {
        const char *cmd_str = command->valuestring;
        if (strcmp(cmd_str, "update_config") == 0) {
            cmd.command = MQTT_CMD_UPDATE_CONFIG;
        } else if (strcmp(cmd_str, "restart") == 0) {
            cmd.command = MQTT_CMD_RESTART;
        } else if (strcmp(cmd_str, "request_status") == 0) {
            cmd.command = MQTT_CMD_REQUEST_STATUS;
        } else if (strcmp(cmd_str, "start_monitor") == 0) {
            cmd.command = MQTT_CMD_START_MONITOR;
        } else if (strcmp(cmd_str, "stop_monitor") == 0) {
            cmd.command = MQTT_CMD_STOP_MONITOR;
        } else if (strcmp(cmd_str, "factory_reset") == 0) {
            cmd.command = MQTT_CMD_FACTORY_RESET;
        }
    }
    
    // timestamp 파싱
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp && cJSON_IsNumber(timestamp)) {
        cmd.timestamp = (uint32_t)timestamp->valuedouble;
    }
    
    // request_id 파싱
    cJSON *request_id = cJSON_GetObjectItem(root, "request_id");
    if (request_id && cJSON_IsString(request_id)) {
        strncpy(cmd.request_id, request_id->valuestring, sizeof(cmd.request_id) - 1);
    }
    
    // payload 내 config_type 파싱
    cJSON *cmd_payload = cJSON_GetObjectItem(root, "payload");
    if (cmd_payload) {
        cJSON *config_type = cJSON_GetObjectItem(cmd_payload, "config_type");
        if (config_type && cJSON_IsString(config_type)) {
            const char *type_str = config_type->valuestring;
            if (strcmp(type_str, "wifi") == 0) cmd.config_type = CONFIG_TYPE_WIFI;
            else if (strcmp(type_str, "mqtt") == 0) cmd.config_type = CONFIG_TYPE_MQTT;
            else if (strcmp(type_str, "uart") == 0) cmd.config_type = CONFIG_TYPE_UART;
            else if (strcmp(type_str, "protocol") == 0) cmd.config_type = CONFIG_TYPE_PROTOCOL;
            else if (strcmp(type_str, "fields") == 0) cmd.config_type = CONFIG_TYPE_FIELDS;
            else cmd.config_type = CONFIG_TYPE_ALL;
        }
    }
    
    // 콜백 호출
    if (s_cmd_callback) {
        s_cmd_callback(&cmd, cmd_payload);
    }
    
    // 응답 전송
    mqtt_handler_send_command_response(cmd.request_id, true, "Command received");
    
    cJSON_Delete(root);
}

/*******************************************************************************
 * Config Download Handler (P0-3)
 ******************************************************************************/
static void handle_config_download(const char *payload, int len)
{
    ESP_LOGI(TAG, "Processing config download");
    
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config download JSON");
        return;
    }
    
    cJSON *update_available = cJSON_GetObjectItem(root, "update_available");
    if (update_available && cJSON_IsBool(update_available)) {
        if (cJSON_IsTrue(update_available)) {
            ESP_LOGI(TAG, "Config update available");
            
            cJSON *config = cJSON_GetObjectItem(root, "config");
            if (config && s_cmd_callback) {
                // config 업데이트 명령으로 전달
                mqtt_remote_command_t cmd = {
                    .command = MQTT_CMD_UPDATE_CONFIG,
                    .config_type = CONFIG_TYPE_ALL,
                    .timestamp = (uint32_t)time(NULL)
                };
                s_cmd_callback(&cmd, config);
            }
        } else {
            ESP_LOGI(TAG, "Config is up to date");
        }
    }
    
    cJSON_Delete(root);
}

/*******************************************************************************
 * Public Functions - Initialization
 ******************************************************************************/
esp_err_t mqtt_handler_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MQTT Handler initialized (v3.0)");
    return ESP_OK;
}

esp_err_t mqtt_handler_start(const mqtt_config_data_t *config)
{
    if (!config || strlen(config->broker) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    mqtt_handler_stop();
    memcpy(&s_config, config, sizeof(mqtt_config_data_t));

    // URI 생성
    char uri[256];
    snprintf(uri, sizeof(uri), "%s://%s:%d",
             config->use_tls ? "mqtts" : "mqtt",
             config->broker, config->port);

    ESP_LOGI(TAG, "Connecting to: %s", uri);
    ESP_LOGI(TAG, "User ID: %s", config->user_id);
    ESP_LOGI(TAG, "Device ID: %s", config->device_id);
    ESP_LOGI(TAG, "JWT Auth: %s", config->use_jwt ? "enabled" : "disabled");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = config->client_id,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 5000,
    };

    if (strlen(config->username) > 0) {
        mqtt_cfg.credentials.username = config->username;
        mqtt_cfg.credentials.authentication.password = config->password;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client start failed: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    return ret;
}

void mqtt_handler_stop(void)
{
    if (s_client) {
        ESP_LOGI(TAG, "Stopping MQTT client...");
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        s_connected = false;
    }
}

bool mqtt_handler_is_connected(void)
{
    return s_connected;
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
static const char* data_type_str(data_type_t type)
{
    switch (type) {
        case DATA_TYPE_BOOL:        return "BOOL";
        case DATA_TYPE_UINT8:       return "UINT8";
        case DATA_TYPE_INT8:        return "INT8";
        case DATA_TYPE_UINT16:      return "UINT16";
        case DATA_TYPE_INT16:       return "INT16";
        case DATA_TYPE_UINT32:      return "UINT32";
        case DATA_TYPE_INT32:       return "INT32";
        case DATA_TYPE_UINT64:      return "UINT64";
        case DATA_TYPE_INT64:       return "INT64";
        case DATA_TYPE_FLOAT32:     return "FLOAT32";
        case DATA_TYPE_FLOAT64:     return "FLOAT64";
        case DATA_TYPE_STRING:      return "STRING";
        case DATA_TYPE_TIMESTAMP:   return "TIMESTAMP";
        default:                    return "UNKNOWN";
    }
}

// Build topic string - v3.0: user_id/device_id 필수 (legacy 토픽 제거)
static void build_topic(char *out, size_t out_size, const char *suffix)
{
    if (strlen(s_config.user_id) > 0 && strlen(s_config.device_id) > 0) {
        // v3.0 SaaS format: user/{user_id}/device/{device_id}/{suffix}
        snprintf(out, out_size, "user/%s/device/%s/%s",
                 s_config.user_id, s_config.device_id, suffix);
    } else {
        // v3.0: user_id/device_id 미설정 시 에러 로그
        ESP_LOGE(TAG, "Cannot build topic: user_id or device_id not set!");
        snprintf(out, out_size, "unconfigured/device/%s/%s", 
                 strlen(s_config.device_id) > 0 ? s_config.device_id : "unknown", suffix);
    }
}

/*******************************************************************************
 * Data Publishing - v2.1 Enhanced
 ******************************************************************************/
esp_err_t mqtt_handler_publish_data(const char *device_id,
                                    const parsed_field_t *fields,
                                    uint8_t field_count,
                                    const uint8_t *raw_data,
                                    size_t raw_len,
                                    uint16_t sequence,
                                    bool crc_valid)    // v2.1: CRC 검증 결과 추가
{
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // v2.1 필수 필드
    const char *dev_id = (strlen(s_config.device_id) > 0) ? s_config.device_id : device_id;
    cJSON_AddStringToObject(root, "device_id", dev_id);
    
    // v2.1: user_id 추가 (필수)
    if (strlen(s_config.user_id) > 0) {
        cJSON_AddStringToObject(root, "user_id", s_config.user_id);
    }
    
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddNumberToObject(root, "sequence", sequence);
    cJSON_AddStringToObject(root, "protocol", "custom");
    
    // v2.1: crc_valid 추가
    cJSON_AddBoolToObject(root, "crc_valid", crc_valid);
    
    // v2.1: schema_version 추가
    cJSON_AddStringToObject(root, "schema_version", SCHEMA_VERSION_STRING);

    // Raw hex 데이터
    if (raw_data && raw_len > 0) {
        char *hex = malloc(raw_len * 2 + 1);
        if (hex) {
            for (size_t i = 0; i < raw_len; i++) {
                sprintf(&hex[i * 2], "%02X", raw_data[i]);
            }
            cJSON_AddStringToObject(root, "raw_hex", hex);
            free(hex);
        }
    }

    // 파싱된 필드
    cJSON *fields_obj = cJSON_CreateObject();
    if (fields_obj) {
        for (uint8_t i = 0; i < field_count; i++) {
            cJSON *field = cJSON_CreateObject();
            if (field) {
                cJSON_AddNumberToObject(field, "value", fields[i].scaled_value);
                cJSON_AddStringToObject(field, "type", data_type_str(fields[i].type));
                // v2.1: raw 값도 추가 (디버깅용)
                if (fields[i].type == DATA_TYPE_UINT32 || fields[i].type == DATA_TYPE_INT32) {
                    cJSON_AddNumberToObject(field, "raw", fields[i].value.u32);
                }
                cJSON_AddItemToObject(fields_obj, fields[i].name, field);
            }
        }
        cJSON_AddItemToObject(root, "fields", fields_obj);
    }

    // JSON 문자열 변환 및 발행
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[256];
        build_topic(topic, sizeof(topic), "data");

        int msg_id = esp_mqtt_client_publish(s_client, topic, json_str,
                                              strlen(json_str), s_config.qos, 0);
        if (msg_id >= 0) {
            s_tx_count++;
            ret = ESP_OK;
            ESP_LOGD(TAG, "Published to %s", topic);
        }
        free(json_str);
    }

    cJSON_Delete(root);
    xSemaphoreGive(s_mutex);
    return ret;
}

/*******************************************************************************
 * Status Publishing - v2.1 Enhanced
 ******************************************************************************/
esp_err_t mqtt_handler_publish_status(const char *device_id,
                                       const device_status_t *status)
{
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // v2.1 필수 필드
    const char *dev_id = (strlen(s_config.device_id) > 0) ? s_config.device_id : device_id;
    cJSON_AddStringToObject(root, "device_id", dev_id);
    
    // v2.1: user_id 추가
    if (strlen(s_config.user_id) > 0) {
        cJSON_AddStringToObject(root, "user_id", s_config.user_id);
    }
    
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    // 연결 상태
    cJSON_AddBoolToObject(root, "wifi_connected", status->wifi_status != 0);
    cJSON_AddNumberToObject(root, "wifi_rssi", status->rssi);
    
    // v3.0: wifi_ip 추가 (인터페이스 정의서 §6.2)
    if (status->wifi_status) {
        char ip_str[16] = {0};
        wifi_manager_get_ip(ip_str, sizeof(ip_str));
        if (strlen(ip_str) > 0) {
            cJSON_AddStringToObject(root, "wifi_ip", ip_str);
        }
    }
    
    cJSON_AddBoolToObject(root, "mqtt_connected", status->mqtt_status != 0);
    cJSON_AddBoolToObject(root, "uart_active", status->uart_status != 0);
    
    // 통계
    cJSON_AddNumberToObject(root, "uptime_seconds", status->uptime);
    cJSON_AddNumberToObject(root, "rx_count", status->rx_count);
    cJSON_AddNumberToObject(root, "tx_count", status->tx_count);
    cJSON_AddNumberToObject(root, "error_count", status->error_count);
    
    // v2.1: free_heap 추가
    cJSON_AddNumberToObject(root, "free_heap", status->free_heap);
    
    // v2.1: config_hash 추가 (설정 동기화용)
    if (strlen(status->config_hash) > 0) {
        cJSON_AddStringToObject(root, "config_hash", status->config_hash);
    }

    // 펌웨어 버전
    char fw[16];
    snprintf(fw, sizeof(fw), "%u.%u.%u",
             (unsigned int)((status->firmware_version >> 24) & 0xFF),
             (unsigned int)((status->firmware_version >> 16) & 0xFF),
             (unsigned int)((status->firmware_version >> 8) & 0xFF));
    cJSON_AddStringToObject(root, "firmware_version", fw);
    
    // v2.1: schema_version 추가
    cJSON_AddStringToObject(root, "schema_version", SCHEMA_VERSION_STRING);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[256];
        build_topic(topic, sizeof(topic), "status");

        int msg_id = esp_mqtt_client_publish(s_client, topic, json_str,
                                              strlen(json_str), s_config.qos, 1);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Status published to %s", topic);
            ret = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to publish status");
        }
        free(json_str);
    }

    cJSON_Delete(root);
    xSemaphoreGive(s_mutex);
    return ret;
}

/*******************************************************************************
 * Config Sync (P0-3)
 ******************************************************************************/
esp_err_t mqtt_handler_request_config_sync(void)
{
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;
    if (strlen(s_config.user_id) == 0 || strlen(s_config.device_id) == 0) {
        ESP_LOGW(TAG, "Cannot request config sync: user_id or device_id not set");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "device_id", s_config.device_id);
    cJSON_AddStringToObject(root, "user_id", s_config.user_id);
    cJSON_AddStringToObject(root, "current_version", SCHEMA_VERSION_STRING);
    cJSON_AddStringToObject(root, "config_hash", "");  // TODO: 실제 해시 계산
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[256];
        build_topic(topic, sizeof(topic), "config/sync");

        int msg_id = esp_mqtt_client_publish(s_client, topic, json_str,
                                              strlen(json_str), s_config.qos, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Config sync request sent to %s", topic);
            ret = ESP_OK;
        }
        free(json_str);
    }

    cJSON_Delete(root);
    xSemaphoreGive(s_mutex);
    return ret;
}

/*******************************************************************************
 * Command Response (P0-3)
 ******************************************************************************/
esp_err_t mqtt_handler_send_command_response(const char *request_id, 
                                              bool success, 
                                              const char *message)
{
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddBoolToObject(root, "success", success);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    if (message) {
        cJSON_AddStringToObject(root, "message", message);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[256];
        build_topic(topic, sizeof(topic), "response");

        int msg_id = esp_mqtt_client_publish(s_client, topic, json_str,
                                              strlen(json_str), s_config.qos, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Command response sent: %s", request_id);
            ret = ESP_OK;
        }
        free(json_str);
    }

    cJSON_Delete(root);
    xSemaphoreGive(s_mutex);
    return ret;
}

/*******************************************************************************
 * Callback Setters
 ******************************************************************************/
uint32_t mqtt_handler_get_tx_count(void)
{
    return s_tx_count;
}

void mqtt_handler_set_callback(mqtt_event_cb_t cb)
{
    s_event_callback = cb;
}

void mqtt_handler_set_cmd_callback(mqtt_cmd_cb_t cb)
{
    s_cmd_callback = cb;
}

const mqtt_config_data_t* mqtt_handler_get_config(void)
{
    return &s_config;
}

/*******************************************************************************
 * Config Upload to Server (sync after BLE config)
 ******************************************************************************/
esp_err_t mqtt_handler_upload_config(const protocol_config_data_t *protocol_config,
                                      const data_definition_t *data_def,
                                      const uart_config_data_t *uart_config)
{
    if (!s_connected || !s_client) {
        ESP_LOGW(TAG, "Cannot upload config: MQTT not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Protocol config
    cJSON *protocol = cJSON_CreateObject();
    if (protocol_config) {
        // Protocol type
        const char *type_str;
        switch (protocol_config->type) {
            case PROTOCOL_CUSTOM: type_str = "custom"; break;
            case PROTOCOL_MODBUS_RTU: type_str = "modbus_rtu"; break;
            case PROTOCOL_MODBUS_ASCII: type_str = "modbus_ascii"; break;
            case PROTOCOL_NMEA_0183: type_str = "nmea_0183"; break;
            case PROTOCOL_IEC_60870_101: type_str = "iec60870_101"; break;
            case PROTOCOL_IEC_60870_104: type_str = "iec60870_104"; break;
            default: type_str = "custom"; break;
        }
        cJSON_AddStringToObject(protocol, "protocolType", type_str);
        
        // Custom protocol config (most common case)
        if (protocol_config->type == PROTOCOL_CUSTOM) {
            const custom_protocol_config_t *custom = &protocol_config->config.custom;
            
            // Frame structure
            cJSON_AddNumberToObject(protocol, "frameLength", custom->frame_length);
            cJSON_AddBoolToObject(protocol, "stxEnabled", custom->stx_enable);
            cJSON_AddNumberToObject(protocol, "stxValue", custom->stx_value);
            cJSON_AddBoolToObject(protocol, "etxEnabled", custom->etx_enable);
            cJSON_AddNumberToObject(protocol, "etxValue", custom->etx_value);
            
            // Length field
            cJSON_AddBoolToObject(protocol, "lengthFieldEnabled", custom->length_field_enable);
            cJSON_AddNumberToObject(protocol, "lengthFieldOffset", custom->length_field_offset);
            cJSON_AddNumberToObject(protocol, "lengthFieldSize", custom->length_field_size);
            cJSON_AddBoolToObject(protocol, "lengthIncludesHeader", custom->length_includes_header);
            
            // CRC
            const char *crc_type_str;
            switch (custom->crc_type) {
                case CRC_NONE: crc_type_str = "none"; break;
                case CRC_XOR_LRC: crc_type_str = "xor"; break;
                case CRC_SUM8: crc_type_str = "sum8"; break;
                case CRC_SUM16: crc_type_str = "sum16"; break;
                case CRC_8: crc_type_str = "crc8"; break;
                case CRC_16_MODBUS: crc_type_str = "crc16_modbus"; break;
                case CRC_16_CCITT: crc_type_str = "crc16_ccitt"; break;
                case CRC_32: crc_type_str = "crc32"; break;
                default: crc_type_str = "none"; break;
            }
            cJSON_AddStringToObject(protocol, "crcType", crc_type_str);
            cJSON_AddNumberToObject(protocol, "crcOffset", custom->crc_offset);
            cJSON_AddNumberToObject(protocol, "crcStartOffset", custom->crc_start_offset);
            cJSON_AddNumberToObject(protocol, "crcEndOffset", custom->crc_end_offset);
            
            // Timeout
            cJSON_AddNumberToObject(protocol, "frameTimeoutMs", custom->timeout_ms);
        }
        
        // UART config (nested)
        if (uart_config) {
            cJSON *uart = cJSON_CreateObject();
            cJSON_AddNumberToObject(uart, "baudrate", uart_config->baudrate);
            cJSON_AddNumberToObject(uart, "dataBits", uart_config->data_bits);
            cJSON_AddNumberToObject(uart, "parity", uart_config->parity);
            cJSON_AddNumberToObject(uart, "stopBits", uart_config->stop_bits);
            cJSON_AddItemToObject(protocol, "uart", uart);
        }
    }
    cJSON_AddItemToObject(root, "protocol", protocol);
    
    // Fields array
    cJSON *fields_array = cJSON_CreateArray();
    if (data_def && data_def->field_count > 0) {
        for (int i = 0; i < data_def->field_count && i < MAX_FIELD_COUNT; i++) {
            const field_definition_t *f = &data_def->fields[i];
            cJSON *field = cJSON_CreateObject();
            
            // Get field name from names table
            char field_name[MAX_FIELD_NAME_LEN + 1] = {0};
            if (f->name_index < data_def->names_length) {
                strncpy(field_name, &data_def->field_names[f->name_index], 
                        f->name_length < MAX_FIELD_NAME_LEN ? f->name_length : MAX_FIELD_NAME_LEN);
            }
            cJSON_AddStringToObject(field, "fieldName", field_name);
            
            // Data type string
            const char *type_str = data_type_str((data_type_t)f->field_type);
            cJSON_AddStringToObject(field, "fieldType", type_str);
            
            cJSON_AddStringToObject(field, "byteOrder", f->byte_order ? "big" : "little");
            cJSON_AddNumberToObject(field, "startOffset", f->start_offset);
            cJSON_AddNumberToObject(field, "bitOffset", f->bit_offset);
            cJSON_AddNumberToObject(field, "bitLength", f->bit_length);
            cJSON_AddNumberToObject(field, "scaleFactor", f->scale_factor / 1000.0);
            cJSON_AddNumberToObject(field, "offsetValue", f->offset_value / 100.0);
            
            cJSON_AddItemToArray(fields_array, field);
        }
    }
    cJSON_AddItemToObject(root, "fields", fields_array);
    
    // Sync version
    cJSON_AddNumberToObject(root, "syncVersion", 1);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[256];
        build_topic(topic, sizeof(topic), "config/upload");

        int msg_id = esp_mqtt_client_publish(s_client, topic, json_str,
                                              strlen(json_str), 1, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Config uploaded to server: %s (%d bytes)", topic, strlen(json_str));
            s_tx_count++;
            ret = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Config upload failed");
        }
        free(json_str);
    }

    cJSON_Delete(root);
    xSemaphoreGive(s_mutex);
    return ret;
}
