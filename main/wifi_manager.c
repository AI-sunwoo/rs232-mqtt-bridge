/**
 * @file wifi_manager.c
 * @brief WiFi Connection Management Implementation
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * v3.0 변경사항:
 * - 지수 백오프 재연결 (1s → 2s → 4s → 8s → 16s → 30s 상한)
 * - 무한 재시도 (MAX_RETRY 제거) - 산업용 장비는 반드시 재연결해야 함
 * - 연결 성공 시 백오프 리셋
 */

#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "WiFi";

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

// 지수 백오프 재연결 설정 (v3.0)
#define BACKOFF_INITIAL_MS      1000    // 초기 1초
#define BACKOFF_MAX_MS          30000   // 최대 30초
#define BACKOFF_MULTIPLIER      2       // 2배씩 증가
#define INITIAL_CONNECT_MAX_RETRY  5    // 초기 연결 시 최대 재시도 (connect() 호출 시)

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_netif = NULL;
static bool s_initialized = false;
static bool s_connected = false;
static bool s_initial_connecting = false;  // connect() 호출 중인지 여부
static int s_retry_count = 0;
static uint32_t s_backoff_ms = BACKOFF_INITIAL_MS;
static wifi_event_cb_t s_callback = NULL;
static TimerHandle_t s_reconnect_timer = NULL;

// Forward declaration
static void reconnect_timer_callback(TimerHandle_t timer);

/*******************************************************************************
 * Reconnect Timer (지수 백오프)
 ******************************************************************************/
static void reconnect_timer_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Reconnect timer fired, attempting connection...");
    esp_wifi_connect();
}

static void schedule_reconnect(void)
{
    if (!s_reconnect_timer) {
        s_reconnect_timer = xTimerCreate("wifi_reconnect", 
                                          pdMS_TO_TICKS(s_backoff_ms),
                                          pdFALSE,  // one-shot
                                          NULL,
                                          reconnect_timer_callback);
    }
    
    if (s_reconnect_timer) {
        // Update timer period to current backoff
        xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(s_backoff_ms), 0);
        xTimerStart(s_reconnect_timer, 0);
        
        ESP_LOGW(TAG, "Reconnect scheduled in %lu ms (attempt %d)", 
                 (unsigned long)s_backoff_ms, s_retry_count);
        
        // Exponential backoff: double the interval, capped at max
        s_backoff_ms *= BACKOFF_MULTIPLIER;
        if (s_backoff_ms > BACKOFF_MAX_MS) {
            s_backoff_ms = BACKOFF_MAX_MS;
        }
    }
}

static void reset_backoff(void)
{
    s_backoff_ms = BACKOFF_INITIAL_MS;
    s_retry_count = 0;
    
    // Stop any pending reconnect timer
    if (s_reconnect_timer) {
        xTimerStop(s_reconnect_timer, 0);
    }
}

/*******************************************************************************
 * WiFi Event Handler
 ******************************************************************************/
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = 
                    (wifi_event_sta_disconnected_t *)event_data;
                s_connected = false;
                s_retry_count++;
                
                ESP_LOGW(TAG, "Disconnected (reason=%d), retry #%d", 
                         event->reason, s_retry_count);
                
                if (s_initial_connecting) {
                    // 초기 연결 시: 제한된 재시도 후 포기 (connect() 블로킹 해제)
                    if (s_retry_count < INITIAL_CONNECT_MAX_RETRY) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_wifi_connect();
                    } else {
                        ESP_LOGE(TAG, "Initial connection failed after %d attempts", 
                                 INITIAL_CONNECT_MAX_RETRY);
                        if (s_wifi_event_group) {
                            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                        }
                        // 초기 연결 실패 후에도 백그라운드 재연결 시작
                        s_initial_connecting = false;
                        schedule_reconnect();
                    }
                } else {
                    // 백그라운드 재연결: 지수 백오프로 무한 재시도
                    schedule_reconnect();
                }
                
                if (s_callback) s_callback(false);
                break;
            }
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_initial_connecting = false;
        
        // 연결 성공 시 백오프 리셋
        reset_backoff();
        
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        if (s_callback) s_callback(true);
    }
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/
esp_err_t wifi_manager_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing (v3.0 - exponential backoff)...");

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const wifi_config_data_t *config)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!config || strlen(config->ssid) == 0) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Connecting to: %s", config->ssid);

    // 기존 연결/타이머 정리
    reset_backoff();
    
    if (s_connected) {
        ESP_LOGI(TAG, "Disconnecting existing connection...");
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, config->ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, config->password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = strlen(config->password) > 0 ? 
                                      WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    s_retry_count = 0;
    s_connected = false;
    s_initial_connecting = true;  // 초기 연결 모드 활성화
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    s_initial_connecting = false;

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected successfully");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Connection failed (will retry in background)");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout (will retry in background)");
        // 타임아웃 시에도 백그라운드 재연결 스케줄링
        schedule_reconnect();
        return ESP_ERR_TIMEOUT;
    }
}

void wifi_manager_disconnect(void)
{
    if (s_initialized) {
        ESP_LOGI(TAG, "Disconnecting...");
        reset_backoff();  // 재연결 타이머 중지
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_connected = false;
    }
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

int8_t wifi_manager_get_rssi(void)
{
    if (!s_connected) return 0;
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

void wifi_manager_get_ip(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len < 16) return;
    ip_str[0] = '\0';

    if (!s_connected || !s_netif) return;

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    }
}

void wifi_manager_set_callback(wifi_event_cb_t cb)
{
    s_callback = cb;
}
