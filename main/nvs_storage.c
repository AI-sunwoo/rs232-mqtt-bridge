/**
 * @file nvs_storage.c
 * @brief NVS Storage Management Implementation
 */

#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS";

// NVS 네임스페이스 (Section 10)
#define NVS_NS_WIFI         "wifi"
#define NVS_NS_MQTT         "mqtt"
#define NVS_NS_UART         "uart"
#define NVS_NS_PROTOCOL     "protocol"
#define NVS_NS_DATA         "data"

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    }
    return ret;
}

/*******************************************************************************
 * WiFi Configuration
 ******************************************************************************/
esp_err_t nvs_save_wifi_config(const wifi_config_data_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(handle, "ssid", config->ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "password", config->password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", config->ssid);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_wifi_config(wifi_config_data_t *config)
{
    memset(config, 0, sizeof(wifi_config_data_t));
    
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_WIFI, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi config in NVS");
        return ret;
    }

    size_t len = sizeof(config->ssid);
    nvs_get_str(handle, "ssid", config->ssid, &len);
    
    len = sizeof(config->password);
    nvs_get_str(handle, "password", config->password, &len);

    ESP_LOGI(TAG, "WiFi config loaded: SSID=%s", config->ssid);
    nvs_close(handle);
    return ESP_OK;
}

/*******************************************************************************
 * MQTT Configuration
 ******************************************************************************/
esp_err_t nvs_save_mqtt_config(const mqtt_config_data_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_MQTT, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_str(handle, "broker", config->broker);
    nvs_set_u16(handle, "port", config->port);
    nvs_set_str(handle, "username", config->username);
    nvs_set_str(handle, "password", config->password);
    nvs_set_str(handle, "client_id", config->client_id);
    nvs_set_str(handle, "topic", config->topic);
    nvs_set_str(handle, "user_id", config->user_id);       // SaaS user ID
    nvs_set_str(handle, "device_id", config->device_id);   // Device ID
    nvs_set_u8(handle, "qos", config->qos);
    nvs_set_u8(handle, "use_tls", config->use_tls ? 1 : 0);

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT config saved: %s:%d (user=%s, device=%s)", 
                 config->broker, config->port, config->user_id, config->device_id);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_mqtt_config(mqtt_config_data_t *config)
{
    memset(config, 0, sizeof(mqtt_config_data_t));
    config->port = DEFAULT_MQTT_PORT;
    config->qos = DEFAULT_MQTT_QOS;
    config->use_tls = true;  // TLS enabled by default

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_MQTT, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No MQTT config in NVS");
        return ret;
    }

    size_t len;
    
    len = sizeof(config->broker);
    nvs_get_str(handle, "broker", config->broker, &len);
    
    nvs_get_u16(handle, "port", &config->port);
    
    len = sizeof(config->username);
    nvs_get_str(handle, "username", config->username, &len);
    
    len = sizeof(config->password);
    nvs_get_str(handle, "password", config->password, &len);
    
    len = sizeof(config->client_id);
    nvs_get_str(handle, "client_id", config->client_id, &len);
    
    len = sizeof(config->topic);
    nvs_get_str(handle, "topic", config->topic, &len);
    
    len = sizeof(config->user_id);
    nvs_get_str(handle, "user_id", config->user_id, &len);
    
    len = sizeof(config->device_id);
    nvs_get_str(handle, "device_id", config->device_id, &len);
    
    nvs_get_u8(handle, "qos", &config->qos);
    
    uint8_t tls;
    if (nvs_get_u8(handle, "use_tls", &tls) == ESP_OK) {
        config->use_tls = (tls != 0);
    }

    ESP_LOGI(TAG, "MQTT config loaded: %s:%d (user=%s, device=%s)", 
             config->broker, config->port, config->user_id, config->device_id);
    nvs_close(handle);
    return ESP_OK;
}

/*******************************************************************************
 * UART Configuration
 ******************************************************************************/
esp_err_t nvs_save_uart_config(const uart_config_data_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_UART, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_u32(handle, "baudrate", config->baudrate);
    
    uint8_t params[4] = {
        config->data_bits,
        config->parity,
        config->stop_bits,
        config->flow_control
    };
    nvs_set_blob(handle, "params", params, sizeof(params));

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "UART config saved: %lu baud", (unsigned long)config->baudrate);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_uart_config(uart_config_data_t *config)
{
    // 기본값 설정
    config->baudrate = DEFAULT_BAUDRATE;
    config->data_bits = DEFAULT_DATA_BITS;
    config->parity = DEFAULT_PARITY;
    config->stop_bits = DEFAULT_STOP_BITS;
    config->flow_control = DEFAULT_FLOW_CONTROL;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_UART, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No UART config in NVS, using defaults");
        return ret;
    }

    nvs_get_u32(handle, "baudrate", &config->baudrate);
    
    uint8_t params[4];
    size_t len = sizeof(params);
    if (nvs_get_blob(handle, "params", params, &len) == ESP_OK) {
        config->data_bits = params[0];
        config->parity = params[1];
        config->stop_bits = params[2];
        config->flow_control = params[3];
    }

    ESP_LOGI(TAG, "UART config loaded: %lu baud", (unsigned long)config->baudrate);
    nvs_close(handle);
    return ESP_OK;
}

/*******************************************************************************
 * Protocol Configuration
 ******************************************************************************/
esp_err_t nvs_save_protocol_config(const protocol_config_data_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_PROTOCOL, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_u8(handle, "type", (uint8_t)config->type);

    // 프로토콜별 설정을 blob으로 저장
    size_t config_size = 0;
    const void *config_data = NULL;

    switch (config->type) {
        case PROTOCOL_CUSTOM:
            config_data = &config->config.custom;
            config_size = sizeof(custom_protocol_config_t);
            break;
        case PROTOCOL_MODBUS_RTU:
        case PROTOCOL_MODBUS_ASCII:
            config_data = &config->config.modbus_rtu;
            config_size = sizeof(modbus_rtu_config_t);
            break;
        case PROTOCOL_NMEA_0183:
            config_data = &config->config.nmea;
            config_size = sizeof(nmea_config_t);
            break;
        case PROTOCOL_IEC_60870_101:
        case PROTOCOL_IEC_60870_104:
            config_data = &config->config.iec60870;
            config_size = sizeof(iec60870_config_t);
            break;
        default:
            break;
    }

    if (config_data && config_size > 0) {
        nvs_set_blob(handle, "config", config_data, config_size);
    }

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Protocol config saved: type=%d", config->type);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_protocol_config(protocol_config_data_t *config)
{
    memset(config, 0, sizeof(protocol_config_data_t));
    config->type = PROTOCOL_CUSTOM;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_PROTOCOL, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No Protocol config in NVS");
        return ret;
    }

    uint8_t type;
    if (nvs_get_u8(handle, "type", &type) == ESP_OK) {
        config->type = (protocol_type_t)type;
    }

    size_t len = 0;
    void *config_data = NULL;

    switch (config->type) {
        case PROTOCOL_CUSTOM:
            config_data = &config->config.custom;
            len = sizeof(custom_protocol_config_t);
            break;
        case PROTOCOL_MODBUS_RTU:
        case PROTOCOL_MODBUS_ASCII:
            config_data = &config->config.modbus_rtu;
            len = sizeof(modbus_rtu_config_t);
            break;
        case PROTOCOL_NMEA_0183:
            config_data = &config->config.nmea;
            len = sizeof(nmea_config_t);
            break;
        case PROTOCOL_IEC_60870_101:
        case PROTOCOL_IEC_60870_104:
            config_data = &config->config.iec60870;
            len = sizeof(iec60870_config_t);
            break;
        default:
            break;
    }

    if (config_data && len > 0) {
        nvs_get_blob(handle, "config", config_data, &len);
    }

    ESP_LOGI(TAG, "Protocol config loaded: type=%d", config->type);
    nvs_close(handle);
    return ESP_OK;
}

/*******************************************************************************
 * Data Definition
 ******************************************************************************/
esp_err_t nvs_save_data_definition(const data_definition_t *def)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_DATA, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_u8(handle, "field_cnt", def->field_count);
    nvs_set_u8(handle, "data_off", def->data_offset);

    if (def->field_count > 0) {
        size_t fields_size = def->field_count * sizeof(field_definition_t);
        nvs_set_blob(handle, "fields", def->fields, fields_size);
    }

    if (def->names_length > 0) {
        nvs_set_blob(handle, "names", def->field_names, def->names_length);
        nvs_set_u16(handle, "names_len", def->names_length);
    }

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Data definition saved: %d fields", def->field_count);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_data_definition(data_definition_t *def)
{
    memset(def, 0, sizeof(data_definition_t));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_DATA, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No Data definition in NVS");
        return ret;
    }

    nvs_get_u8(handle, "field_cnt", &def->field_count);
    nvs_get_u8(handle, "data_off", &def->data_offset);

    if (def->field_count > 0 && def->field_count <= MAX_FIELD_COUNT) {
        size_t len = def->field_count * sizeof(field_definition_t);
        nvs_get_blob(handle, "fields", def->fields, &len);
    }

    nvs_get_u16(handle, "names_len", &def->names_length);
    if (def->names_length > 0 && def->names_length <= MAX_FIELD_NAMES_SIZE) {
        size_t len = def->names_length;
        nvs_get_blob(handle, "names", def->field_names, &len);
    }

    ESP_LOGI(TAG, "Data definition loaded: %d fields", def->field_count);
    nvs_close(handle);
    return ESP_OK;
}

/*******************************************************************************
 * Factory Reset
 ******************************************************************************/
esp_err_t nvs_reset_to_defaults(void)
{
    ESP_LOGW(TAG, "Factory reset...");
    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_flash_init();
    ESP_LOGI(TAG, "Factory reset complete");
    return ret;
}

bool nvs_is_configured(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t len = 0;
    bool configured = (nvs_get_str(handle, "ssid", NULL, &len) == ESP_OK && len > 1);
    nvs_close(handle);
    return configured;
}
