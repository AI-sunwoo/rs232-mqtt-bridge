/**
 * @file mqtt_handler.c
 * @brief MQTT Client Handler Implementation
 * 
 * MQTT 메시지 포맷은 Protocol Spec Section 8 참조
 */

#include "mqtt_handler.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static mqtt_event_cb_t s_callback = NULL;
static mqtt_config_data_t s_config = {0};
static uint32_t s_tx_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

static void mqtt_event_handler(void *args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            s_connected = true;
            
            // Subscribe to command topic
            char cmd_topic[MQTT_TOPIC_MAX_LEN + 16];
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/cmd", s_config.topic);
            esp_mqtt_client_subscribe(s_client, cmd_topic, s_config.qos);
            ESP_LOGI(TAG, "Subscribed: %s", cmd_topic);
            
            if (s_callback) s_callback(true);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected");
            s_connected = false;
            if (s_callback) s_callback(false);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Data received: %.*s", event->topic_len, event->topic);
            // TODO: 원격 명령 처리
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error occurred");
            break;

        default:
            break;
    }
}

esp_err_t mqtt_handler_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Initialized");
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
        ESP_LOGE(TAG, "Init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start failed: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    return ret;
}

void mqtt_handler_stop(void)
{
    if (s_client) {
        ESP_LOGI(TAG, "Stopping...");
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
        case DATA_TYPE_FLOAT32:     return "FLOAT32";
        case DATA_TYPE_FLOAT64:     return "FLOAT64";
        default:                    return "UNKNOWN";
    }
}

// Section 8.2: Data Message (JSON)
esp_err_t mqtt_handler_publish_data(const char *device_id,
                                    const parsed_field_t *fields,
                                    uint8_t field_count,
                                    const uint8_t *raw_data,
                                    size_t raw_len,
                                    uint16_t sequence)
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

    // 기본 정보
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddNumberToObject(root, "sequence", sequence);

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
                cJSON_AddItemToObject(fields_obj, fields[i].name, field);
            }
        }
        cJSON_AddItemToObject(root, "fields", fields_obj);
    }

    // JSON 문자열 변환 및 발행
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[MQTT_TOPIC_MAX_LEN + 16];
        snprintf(topic, sizeof(topic), "%s/data", s_config.topic);

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

// Section 8.3: Status Message (JSON)
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

    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "wifi_rssi", status->rssi);
    cJSON_AddBoolToObject(root, "mqtt_connected", status->mqtt_status != 0);
    cJSON_AddBoolToObject(root, "uart_active", status->uart_status != 0);
    cJSON_AddNumberToObject(root, "uptime_seconds", status->uptime);
    cJSON_AddNumberToObject(root, "rx_count", status->rx_count);
    cJSON_AddNumberToObject(root, "tx_count", status->tx_count);
    cJSON_AddNumberToObject(root, "error_count", status->error_count);

    char fw[16];
    snprintf(fw, sizeof(fw), "%u.%u.%u",
             (unsigned int)((status->firmware_version >> 24) & 0xFF),
             (unsigned int)((status->firmware_version >> 16) & 0xFF),
             (unsigned int)((status->firmware_version >> 8) & 0xFF));
    cJSON_AddStringToObject(root, "firmware", fw);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        char topic[MQTT_TOPIC_MAX_LEN + 16];
        snprintf(topic, sizeof(topic), "%s/status", s_config.topic);

        int msg_id = esp_mqtt_client_publish(s_client, topic, json_str,
                                              strlen(json_str), s_config.qos, 1);
        if (msg_id >= 0) {
            ret = ESP_OK;
        }
        free(json_str);
    }

    cJSON_Delete(root);
    xSemaphoreGive(s_mutex);
    return ret;
}

uint32_t mqtt_handler_get_tx_count(void)
{
    return s_tx_count;
}

void mqtt_handler_set_callback(mqtt_event_cb_t cb)
{
    s_callback = cb;
}
