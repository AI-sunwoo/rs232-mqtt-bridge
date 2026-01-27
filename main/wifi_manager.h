/**
 * @file wifi_manager.h
 * @brief WiFi Connection Management
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_event_cb_t)(bool connected);

/**
 * @brief WiFi 초기화
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief WiFi 연결
 */
esp_err_t wifi_manager_connect(const wifi_config_data_t *config);

/**
 * @brief WiFi 연결 해제
 */
void wifi_manager_disconnect(void);

/**
 * @brief WiFi 연결 상태 확인
 */
bool wifi_manager_is_connected(void);

/**
 * @brief WiFi RSSI 반환
 */
int8_t wifi_manager_get_rssi(void);

/**
 * @brief IP 주소 문자열 반환
 */
void wifi_manager_get_ip(char *ip_str, size_t max_len);

/**
 * @brief 이벤트 콜백 설정
 */
void wifi_manager_set_callback(wifi_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
