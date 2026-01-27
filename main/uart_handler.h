/**
 * @file uart_handler.h
 * @brief UART Data Reception Handler
 */

#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*uart_frame_cb_t)(const uint8_t *data, size_t length);

/**
 * @brief UART 초기화
 */
esp_err_t uart_handler_init(void);

/**
 * @brief UART 시작
 */
esp_err_t uart_handler_start(const uart_config_data_t *uart_cfg,
                             const protocol_config_data_t *proto_cfg);

/**
 * @brief UART 중지
 */
void uart_handler_stop(void);

/**
 * @brief 데이터 수신 중인지 확인
 */
bool uart_handler_is_receiving(void);

/**
 * @brief 수신 프레임 카운트
 */
uint32_t uart_handler_get_rx_count(void);

/**
 * @brief 에러 카운트
 */
uint32_t uart_handler_get_error_count(void);

/**
 * @brief 프레임 수신 콜백 설정
 */
void uart_handler_set_callback(uart_frame_cb_t cb);

/**
 * @brief 프로토콜 설정 업데이트
 */
esp_err_t uart_handler_update_protocol(const protocol_config_data_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // UART_HANDLER_H
