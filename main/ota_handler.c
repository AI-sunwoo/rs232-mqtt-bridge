/**
 * @file ota_handler.c
 * @brief OTA (Over-The-Air) Update Handler Implementation
 */

#include "ota_handler.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "OTA_HANDLER";

/*******************************************************************************
 * Configuration - GitHub 저장소 설정
 ******************************************************************************/
#define OTA_VERSION_URL     "https://raw.githubusercontent.com/AI-sunwoo/rs232-mqtt-bridge/main/firmware/version.json"
#define OTA_TASK_STACK      8192
#define OTA_TASK_PRIORITY   5
#define OTA_BUFFER_SIZE     4096
#define OTA_TIMEOUT_MS      30000

// 현재 펌웨어 버전 (빌드 시 자동 설정 가능)
#define FIRMWARE_VERSION    "1.0.0"

/*******************************************************************************
 * Embedded CA Certificate for HTTPS
 * GitHub의 루트 CA 인증서 (DigiCert Global Root CA)
 * 프로덕션에서는 인증서를 활성화하세요
 ******************************************************************************/
#ifdef CONFIG_OTA_USE_CERT
extern const uint8_t server_cert_pem_start[] asm("_binary_github_ca_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_github_ca_pem_end");
#define OTA_CERT_PEM  (const char *)server_cert_pem_start
#else
#define OTA_CERT_PEM  NULL
#endif

// 개발 중 인증서 검증 스킵 (프로덕션에서는 false로 설정)
#define OTA_SKIP_CERT_VERIFY  true

/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static ota_state_t s_ota_state = OTA_STATE_IDLE;
static ota_error_t s_ota_error = OTA_ERR_NONE;
static ota_version_info_t s_version_info = {0};
static ota_progress_cb_t s_progress_callback = NULL;
static TaskHandle_t s_ota_task_handle = NULL;
static SemaphoreHandle_t s_ota_mutex = NULL;
static bool s_abort_requested = false;
static uint8_t s_progress = 0;

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
static void notify_progress(ota_state_t state, uint8_t progress, ota_error_t error)
{
    s_ota_state = state;
    s_progress = progress;
    s_ota_error = error;
    
    if (s_progress_callback) {
        s_progress_callback(state, progress, error);
    }
}

static bool is_wifi_connected(void)
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

static int compare_versions(const char *v1, const char *v2)
{
    int major1, minor1, patch1;
    int major2, minor2, patch2;
    
    if (sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1) != 3) {
        return 0;
    }
    if (sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2) != 3) {
        return 0;
    }
    
    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

/*******************************************************************************
 * Version Check
 ******************************************************************************/
static esp_err_t fetch_version_info(void)
{
    esp_err_t ret = ESP_FAIL;
    char *response_buffer = NULL;
    
    response_buffer = malloc(2048);
    if (!response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }
    
    esp_http_client_config_t config = {
        .url = OTA_VERSION_URL,
        .timeout_ms = OTA_TIMEOUT_MS,
        .buffer_size = 2048,
        .cert_pem = OTA_CERT_PEM,
        .skip_cert_common_name_check = OTA_SKIP_CERT_VERIFY,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(response_buffer);
        return ESP_FAIL;
    }
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch headers");
        goto cleanup;
    }
    
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        goto cleanup;
    }
    
    int read_len = esp_http_client_read(client, response_buffer, 2047);
    if (read_len <= 0) {
        ESP_LOGE(TAG, "Failed to read response");
        goto cleanup;
    }
    response_buffer[read_len] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(response_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        goto cleanup;
    }
    
    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url = cJSON_GetObjectItem(root, "url");
    cJSON *size = cJSON_GetObjectItem(root, "size");
    
    if (version && cJSON_IsString(version)) {
        strncpy(s_version_info.latest_version, version->valuestring, 
                sizeof(s_version_info.latest_version) - 1);
    }
    
    if (url && cJSON_IsString(url)) {
        strncpy(s_version_info.firmware_url, url->valuestring,
                sizeof(s_version_info.firmware_url) - 1);
    }
    
    if (size && cJSON_IsNumber(size)) {
        s_version_info.firmware_size = (uint32_t)size->valuedouble;
    }
    
    // 버전 비교
    strncpy(s_version_info.current_version, FIRMWARE_VERSION, 
            sizeof(s_version_info.current_version) - 1);
    
    s_version_info.update_available = 
        (compare_versions(s_version_info.latest_version, s_version_info.current_version) > 0);
    
    ESP_LOGI(TAG, "Current: %s, Latest: %s, Update: %s",
             s_version_info.current_version,
             s_version_info.latest_version,
             s_version_info.update_available ? "Yes" : "No");
    
    cJSON_Delete(root);
    ret = ESP_OK;
    
cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ret;
}

/*******************************************************************************
 * OTA Download and Flash
 ******************************************************************************/
static void ota_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OTA task started");
    
    // WiFi 연결 확인
    if (!is_wifi_connected()) {
        ESP_LOGE(TAG, "WiFi not connected");
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_WIFI_NOT_CONNECTED);
        goto task_exit;
    }
    
    // 버전 확인
    notify_progress(OTA_STATE_CHECKING, 0, OTA_ERR_NONE);
    
    if (fetch_version_info() != ESP_OK) {
        ESP_LOGE(TAG, "Version check failed");
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_VERSION_CHECK_FAILED);
        goto task_exit;
    }
    
    if (!s_version_info.update_available) {
        ESP_LOGI(TAG, "Already at latest version");
        notify_progress(OTA_STATE_NO_UPDATE, 100, OTA_ERR_ALREADY_LATEST);
        goto task_exit;
    }
    
    // 중단 요청 확인
    if (s_abort_requested) {
        ESP_LOGW(TAG, "OTA aborted by user");
        notify_progress(OTA_STATE_IDLE, 0, OTA_ERR_NONE);
        goto task_exit;
    }
    
    // 다운로드 시작
    notify_progress(OTA_STATE_DOWNLOADING, 0, OTA_ERR_NONE);
    ESP_LOGI(TAG, "Downloading from: %s", s_version_info.firmware_url);
    
    esp_http_client_config_t http_config = {
        .url = s_version_info.firmware_url,
        .timeout_ms = OTA_TIMEOUT_MS,
        .buffer_size = OTA_BUFFER_SIZE,
        .buffer_size_tx = 1024,
        .cert_pem = OTA_CERT_PEM,
        .skip_cert_common_name_check = OTA_SKIP_CERT_VERIFY,
        .keep_alive_enable = true,
    };
    
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .bulk_flash_erase = false,
        .partial_http_download = true,
        .max_http_request_size = OTA_BUFFER_SIZE,
    };
    
    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_DOWNLOAD_FAILED);
        goto task_exit;
    }
    
    // 이미지 정보 확인
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get image desc: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_DOWNLOAD_FAILED);
        goto task_exit;
    }
    
    ESP_LOGI(TAG, "New firmware: %s, version: %s", app_desc.project_name, app_desc.version);
    
    // 다운로드 진행
    int total_size = esp_https_ota_get_image_size(https_ota_handle);
    int downloaded = 0;
    
    while (1) {
        if (s_abort_requested) {
            ESP_LOGW(TAG, "OTA aborted during download");
            esp_https_ota_abort(https_ota_handle);
            notify_progress(OTA_STATE_IDLE, 0, OTA_ERR_NONE);
            goto task_exit;
        }
        
        err = esp_https_ota_perform(https_ota_handle);
        
        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            downloaded = esp_https_ota_get_image_len_read(https_ota_handle);
            uint8_t progress = (total_size > 0) ? (downloaded * 100 / total_size) : 0;
            notify_progress(OTA_STATE_DOWNLOADING, progress, OTA_ERR_NONE);
            ESP_LOGD(TAG, "Downloaded %d / %d bytes (%d%%)", downloaded, total_size, progress);
            continue;
        }
        
        if (err == ESP_OK) {
            break;  // 다운로드 완료
        }
        
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_DOWNLOAD_FAILED);
        goto task_exit;
    }
    
    // 검증
    notify_progress(OTA_STATE_VERIFYING, 95, OTA_ERR_NONE);
    
    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Incomplete data received");
        esp_https_ota_abort(https_ota_handle);
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_DOWNLOAD_FAILED);
        goto task_exit;
    }
    
    // OTA 완료 및 적용
    notify_progress(OTA_STATE_APPLYING, 98, OTA_ERR_NONE);
    
    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Firmware validation failed");
            notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_SIGNATURE_INVALID);
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
            notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_FLASH_FAILED);
        }
        goto task_exit;
    }
    
    ESP_LOGI(TAG, "OTA update successful! Rebooting in 3 seconds...");
    notify_progress(OTA_STATE_SUCCESS, 100, OTA_ERR_NONE);
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    
task_exit:
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/
esp_err_t ota_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA handler...");
    
    if (s_ota_mutex == NULL) {
        s_ota_mutex = xSemaphoreCreateMutex();
        if (s_ota_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // 현재 버전 설정
    strncpy(s_version_info.current_version, FIRMWARE_VERSION, 
            sizeof(s_version_info.current_version) - 1);
    
    // 앱 정보에서 버전 가져오기 (가능한 경우)
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        strncpy(s_version_info.current_version, app_desc->version,
                sizeof(s_version_info.current_version) - 1);
        ESP_LOGI(TAG, "Firmware: %s v%s", app_desc->project_name, app_desc->version);
    }
    
    // 부팅 후 펌웨어 유효성 확인 (롤백 지원)
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "First boot after OTA - marking as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    
    ESP_LOGI(TAG, "Running from partition: %s", running->label);
    ESP_LOGI(TAG, "OTA handler initialized");
    
    return ESP_OK;
}

void ota_handler_set_callback(ota_progress_cb_t callback)
{
    s_progress_callback = callback;
}

esp_err_t ota_handler_check_version(void)
{
    if (!is_wifi_connected()) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    if (s_ota_state != OTA_STATE_IDLE && s_ota_state != OTA_STATE_NO_UPDATE) {
        return ESP_ERR_INVALID_STATE;
    }
    
    notify_progress(OTA_STATE_CHECKING, 0, OTA_ERR_NONE);
    
    esp_err_t ret = fetch_version_info();
    
    if (ret == ESP_OK) {
        if (s_version_info.update_available) {
            notify_progress(OTA_STATE_IDLE, 0, OTA_ERR_NONE);
        } else {
            notify_progress(OTA_STATE_NO_UPDATE, 100, OTA_ERR_ALREADY_LATEST);
        }
    } else {
        notify_progress(OTA_STATE_FAILED, 0, OTA_ERR_VERSION_CHECK_FAILED);
    }
    
    return ret;
}

esp_err_t ota_handler_start(void)
{
    if (s_ota_task_handle != NULL) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_wifi_connected()) {
        ESP_LOGE(TAG, "WiFi not connected");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    s_abort_requested = false;
    
    BaseType_t ret = xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK, 
                                  NULL, OTA_TASK_PRIORITY, &s_ota_task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

void ota_handler_abort(void)
{
    if (s_ota_task_handle != NULL) {
        s_abort_requested = true;
        ESP_LOGI(TAG, "OTA abort requested");
    }
}

ota_state_t ota_handler_get_state(void)
{
    return s_ota_state;
}

const ota_version_info_t* ota_handler_get_version_info(void)
{
    return &s_version_info;
}

const char* ota_handler_get_current_version(void)
{
    return s_version_info.current_version;
}

esp_err_t ota_handler_rollback(void)
{
    if (!ota_handler_can_rollback()) {
        ESP_LOGE(TAG, "Rollback not available");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGW(TAG, "Rolling back to previous firmware...");
    
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // 재부팅되므로 여기에 도달하지 않음
    return ESP_OK;
}

esp_err_t ota_handler_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "App marked as valid");
    }
    return err;
}

bool ota_handler_can_rollback(void)
{
    esp_ota_img_states_t ota_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
    
    if (last_invalid != NULL) {
        return true;
    }
    
    // 현재 파티션이 OTA 파티션인지 확인
    if (running->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        running->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
        return true;
    }
    
    return false;
}
