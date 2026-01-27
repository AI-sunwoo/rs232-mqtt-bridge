/**
 * @file ota_handler.h
 * @brief OTA (Over-The-Air) Update Handler with GitHub Integration
 * 
 * Features:
 * - GitHub Releases integration
 * - Version checking
 * - Secure firmware download (HTTPS)
 * - Firmware signature verification
 * - Rollback support
 */

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA 상태
 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_APPLYING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED,
    OTA_STATE_NO_UPDATE,
} ota_state_t;

/**
 * @brief OTA 에러 코드
 */
typedef enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_WIFI_NOT_CONNECTED,
    OTA_ERR_VERSION_CHECK_FAILED,
    OTA_ERR_ALREADY_LATEST,
    OTA_ERR_DOWNLOAD_FAILED,
    OTA_ERR_SIGNATURE_INVALID,
    OTA_ERR_FLASH_FAILED,
    OTA_ERR_ROLLBACK_FAILED,
    OTA_ERR_BUSY,
} ota_error_t;

/**
 * @brief OTA 진행 상황 콜백
 */
typedef void (*ota_progress_cb_t)(ota_state_t state, uint8_t progress, ota_error_t error);

/**
 * @brief OTA 버전 정보
 */
typedef struct {
    char current_version[16];   // 현재 펌웨어 버전
    char latest_version[16];    // 서버의 최신 버전
    char firmware_url[256];     // 펌웨어 다운로드 URL
    uint32_t firmware_size;     // 펌웨어 크기 (bytes)
    bool update_available;      // 업데이트 가능 여부
} ota_version_info_t;

/**
 * @brief OTA 핸들러 초기화
 * @return ESP_OK on success
 */
esp_err_t ota_handler_init(void);

/**
 * @brief 진행 상황 콜백 설정
 * @param callback 콜백 함수
 */
void ota_handler_set_callback(ota_progress_cb_t callback);

/**
 * @brief 버전 정보 확인 (비동기)
 * @return ESP_OK if check started
 */
esp_err_t ota_handler_check_version(void);

/**
 * @brief OTA 업데이트 시작 (비동기)
 * @return ESP_OK if OTA started
 */
esp_err_t ota_handler_start(void);

/**
 * @brief OTA 업데이트 중단
 */
void ota_handler_abort(void);

/**
 * @brief 현재 OTA 상태 반환
 */
ota_state_t ota_handler_get_state(void);

/**
 * @brief 버전 정보 반환
 */
const ota_version_info_t* ota_handler_get_version_info(void);

/**
 * @brief 현재 펌웨어 버전 반환
 */
const char* ota_handler_get_current_version(void);

/**
 * @brief 이전 버전으로 롤백
 * @return ESP_OK on success
 */
esp_err_t ota_handler_rollback(void);

/**
 * @brief 현재 펌웨어를 유효한 것으로 마킹
 * @return ESP_OK on success
 */
esp_err_t ota_handler_mark_valid(void);

/**
 * @brief 롤백 가능 여부 확인
 */
bool ota_handler_can_rollback(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_HANDLER_H
