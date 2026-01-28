/**
 * @file uart_handler.c
 * @brief UART Data Reception Handler Implementation
 * 
 * 프로토콜별 프레임 검출 및 CRC 검증
 */

#include "uart_handler.h"
#include "protocol_def.h"
#include "crc_utils.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "UART";

static TaskHandle_t s_task = NULL;
static QueueHandle_t s_queue = NULL;
static bool s_running = false;
static bool s_receiving = false;
static uint32_t s_rx_count = 0;
static uint32_t s_error_count = 0;
static uart_frame_cb_t s_callback = NULL;

static protocol_config_data_t s_proto_cfg = {0};
static uint8_t s_frame_buf[FRAME_BUF_SIZE];
static size_t s_frame_idx = 0;
static TickType_t s_last_rx = 0;

// CRC 검증
static bool verify_crc(const uint8_t *data, size_t len)
{
    if (s_proto_cfg.type == PROTOCOL_CUSTOM) {
        custom_protocol_config_t *cfg = &s_proto_cfg.config.custom;
        
        if (cfg->crc_type == CRC_NONE) return true;

        size_t crc_start = cfg->crc_start_offset;
        size_t crc_end = cfg->crc_end_offset;
        if (crc_end == 0 || crc_end > len) {
            crc_end = cfg->crc_offset;
        }

        size_t crc_len = crc_end - crc_start;
        if (crc_len == 0 || crc_start >= len) return true;

        uint32_t calc = crc_calculate(cfg->crc_type, &data[crc_start], crc_len);
        uint8_t crc_size = crc_get_size(cfg->crc_type);

        if (cfg->crc_offset + crc_size > len) return false;

        uint32_t recv = 0;
        for (uint8_t i = 0; i < crc_size; i++) {
            recv |= ((uint32_t)data[cfg->crc_offset + i]) << (i * 8);
        }

        return (calc == recv);
    }
    else if (s_proto_cfg.type == PROTOCOL_MODBUS_RTU) {
        if (len < 4) return false;
        uint16_t calc = crc_calc_crc16_modbus(data, len - 2);
        uint16_t recv = data[len - 2] | (data[len - 1] << 8);
        return (calc == recv);
    }
    else if (s_proto_cfg.type == PROTOCOL_MODBUS_ASCII) {
        // Modbus ASCII: LRC (Longitudinal Redundancy Check)
        // 프레임 형식: ':' ADDR(2) FUNC(2) DATA... LRC(2) CR LF
        if (len < 9) return false;
        
        // LRC 계산: 주소부터 데이터 끝까지의 각 바이트 합의 2의 보수
        // ASCII로 인코딩된 헥사 값을 바이너리로 변환 후 계산
        uint8_t lrc = 0;
        for (size_t i = 1; i < len - 4; i += 2) {
            char hex[3] = {data[i], data[i + 1], 0};
            lrc += (uint8_t)strtol(hex, NULL, 16);
        }
        lrc = (~lrc + 1) & 0xFF;  // 2's complement
        
        // 수신된 LRC
        char lrc_hex[3] = {data[len - 4], data[len - 3], 0};
        uint8_t recv_lrc = (uint8_t)strtol(lrc_hex, NULL, 16);
        
        return (lrc == recv_lrc);
    }
    else if (s_proto_cfg.type == PROTOCOL_NMEA_0183) {
        if (!s_proto_cfg.config.nmea.validate_checksum) return true;
        // NMEA: $....*XX\r\n
        for (size_t i = 1; i < len - 3; i++) {
            if (data[i] == '*') {
                uint8_t calc = 0;
                for (size_t j = 1; j < i; j++) {
                    calc ^= data[j];
                }
                char hex[3] = {data[i + 1], data[i + 2], 0};
                uint8_t recv = (uint8_t)strtol(hex, NULL, 16);
                return (calc == recv);
            }
        }
        return false;
    }
    else if (s_proto_cfg.type == PROTOCOL_IEC_60870_101) {
        // IEC 60870-5-101 체크섬 검증
        // 고정 프레임 (0x10): CS = (CF + AF) mod 256
        // 가변 프레임 (0x68): CS = sum of user data mod 256
        
        if (len == 1 && data[0] == 0xE5) {
            return true;  // Single character ACK - no checksum
        }
        
        if (len >= 5 && data[0] == 0x10) {
            // 고정 프레임: 10 CF AF CS 16
            uint8_t calc = (data[1] + data[2]) & 0xFF;
            return (calc == data[3] && data[4] == 0x16);
        }
        
        if (len >= 6 && data[0] == 0x68) {
            // 가변 프레임: 68 L L 68 [User Data] CS 16
            uint8_t frame_len = data[1];
            if (data[2] != frame_len || data[3] != 0x68) return false;
            
            // 체크섬: User Data 합
            uint8_t calc = 0;
            for (size_t i = 4; i < 4 + frame_len; i++) {
                calc += data[i];
            }
            
            size_t cs_pos = 4 + frame_len;
            if (cs_pos >= len - 1) return false;
            
            return (calc == data[cs_pos] && data[cs_pos + 1] == 0x16);
        }
        
        return false;
    }

    return true;
}

// 프레임 완료 검사
static bool is_frame_complete(const uint8_t *data, size_t len)
{
    if (len == 0) return false;

    if (s_proto_cfg.type == PROTOCOL_CUSTOM) {
        custom_protocol_config_t *cfg = &s_proto_cfg.config.custom;

        // 고정 길이
        if (cfg->frame_length > 0 && len >= cfg->frame_length) {
            return true;
        }

        // STX/ETX 체크
        if (cfg->stx_enable && cfg->etx_enable) {
            if (cfg->etx_value <= 0xFF) {
                if (data[len - 1] == (uint8_t)cfg->etx_value) return true;
            } else {
                if (len >= 2 &&
                    data[len - 2] == ((cfg->etx_value >> 8) & 0xFF) &&
                    data[len - 1] == (cfg->etx_value & 0xFF)) {
                    return true;
                }
            }
        }

        // Length 필드 체크
        if (cfg->length_field_enable && len > cfg->length_field_offset) {
            uint16_t frame_len = 0;
            if (cfg->length_field_size == 1) {
                frame_len = data[cfg->length_field_offset];
            } else if (cfg->length_field_size == 2 && 
                       len > cfg->length_field_offset + 1) {
                frame_len = data[cfg->length_field_offset] |
                           (data[cfg->length_field_offset + 1] << 8);
            }
            
            if (cfg->length_includes_header) {
                if (len >= frame_len) return true;
            } else {
                if (len >= frame_len + cfg->length_field_offset + cfg->length_field_size) {
                    return true;
                }
            }
        }
    }
    else if (s_proto_cfg.type == PROTOCOL_MODBUS_RTU) {
        // Modbus RTU: 타임아웃 기반 프레임 검출
        if (len >= 4) {
            TickType_t now = xTaskGetTickCount();
            uint16_t timeout = s_proto_cfg.config.modbus_rtu.inter_frame_delay;
            if (timeout == 0) timeout = 4;
            if ((now - s_last_rx) >= pdMS_TO_TICKS(timeout)) {
                return true;
            }
        }
    }
    else if (s_proto_cfg.type == PROTOCOL_MODBUS_ASCII) {
        // Modbus ASCII: ':' 시작, CR+LF 종료
        if (len >= 9 && data[0] == ':' &&
            data[len - 2] == '\r' && data[len - 1] == '\n') {
            return true;
        }
    }
    else if (s_proto_cfg.type == PROTOCOL_NMEA_0183) {
        // NMEA: $...*XX\r\n
        if (len >= 6 && data[0] == '$' &&
            data[len - 2] == '\r' && data[len - 1] == '\n') {
            return true;
        }
    }
    else if (s_proto_cfg.type == PROTOCOL_IEC_60870_101) {
        // IEC 60870-5-101 (SCADA 시리얼)
        // 가변 길이 프레임: 0x68 L L 0x68 [CF AF] [ASDU] CS 0x16
        // 고정 길이 프레임: 0x10 CF AF CS 0x16
        // 단일 문자: 0xE5 (ACK)
        
        if (len == 1 && data[0] == 0xE5) {
            return true;  // Single character ACK
        }
        
        if (len >= 5 && data[0] == 0x10) {
            // 고정 길이 프레임 (5 bytes: 10 CF AF CS 16)
            if (len >= 5 && data[len - 1] == 0x16) {
                return true;
            }
        }
        
        if (len >= 6 && data[0] == 0x68) {
            // 가변 길이 프레임
            uint8_t frame_len = data[1];
            // 전체 길이 = 4 (header) + L + 2 (CS + end)
            if (data[2] == frame_len && data[3] == 0x68) {
                size_t total_len = 4 + frame_len + 2;
                if (len >= total_len && data[len - 1] == 0x16) {
                    return true;
                }
            }
        }
    }

    return false;
}

// 프레임 처리
static void process_frame(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "Frame received: %d bytes", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len > 32 ? 32 : len, ESP_LOG_DEBUG);

    if (!verify_crc(data, len)) {
        ESP_LOGW(TAG, "CRC error");
        s_error_count++;
        return;
    }

    s_rx_count++;
    s_receiving = true;

    if (s_callback) {
        s_callback(data, len);
    }
}

// UART 수신 태스크
static void uart_rx_task(void *arg)
{
    uart_event_t event;
    uint8_t rx_buf[128];

    ESP_LOGI(TAG, "RX task started");

    while (s_running) {
        if (xQueueReceive(s_queue, &event, pdMS_TO_TICKS(100))) {
            switch (event.type) {
                case UART_DATA: {
                    int len = uart_read_bytes(UART_PORT_NUM, rx_buf,
                                              event.size, pdMS_TO_TICKS(100));
                    if (len > 0) {
                        s_last_rx = xTaskGetTickCount();
                        
                        for (int i = 0; i < len && s_frame_idx < FRAME_BUF_SIZE; i++) {
                            s_frame_buf[s_frame_idx++] = rx_buf[i];
                        }

                        if (is_frame_complete(s_frame_buf, s_frame_idx)) {
                            process_frame(s_frame_buf, s_frame_idx);
                            s_frame_idx = 0;
                        }
                    }
                    break;
                }

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "Buffer overflow");
                    uart_flush_input(UART_PORT_NUM);
                    xQueueReset(s_queue);
                    s_frame_idx = 0;
                    s_error_count++;
                    break;

                case UART_PARITY_ERR:
                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART error");
                    s_error_count++;
                    break;

                default:
                    break;
            }
        } else {
            // 타임아웃 - 프레임 타임아웃 체크
            if (s_frame_idx > 0) {
                TickType_t now = xTaskGetTickCount();
                uint16_t timeout = 100;

                if (s_proto_cfg.type == PROTOCOL_CUSTOM) {
                    timeout = s_proto_cfg.config.custom.timeout_ms;
                    if (timeout == 0) timeout = 100;
                } else if (s_proto_cfg.type == PROTOCOL_MODBUS_RTU) {
                    timeout = s_proto_cfg.config.modbus_rtu.inter_frame_delay;
                    if (timeout == 0) timeout = 10;
                }

                if ((now - s_last_rx) >= pdMS_TO_TICKS(timeout)) {
                    if (s_frame_idx >= 3) {
                        process_frame(s_frame_buf, s_frame_idx);
                    }
                    s_frame_idx = 0;
                }
            }

            // 수신 상태 리셋
            if (s_receiving && (xTaskGetTickCount() - s_last_rx) > pdMS_TO_TICKS(1000)) {
                s_receiving = false;
            }
        }
    }

    ESP_LOGI(TAG, "RX task stopped");
    vTaskDelete(NULL);
}

esp_err_t uart_handler_init(void)
{
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t uart_handler_start(const uart_config_data_t *uart_cfg,
                             const protocol_config_data_t *proto_cfg)
{
    if (!uart_cfg) return ESP_ERR_INVALID_ARG;

    uart_handler_stop();

    if (proto_cfg) {
        memcpy(&s_proto_cfg, proto_cfg, sizeof(protocol_config_data_t));
    }

    // UART 설정
    uart_config_t cfg = {
        .baud_rate = uart_cfg->baudrate,
        .data_bits = (uart_cfg->data_bits == 7) ? UART_DATA_7_BITS : UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = (uart_cfg->stop_bits == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    switch (uart_cfg->parity) {
        case 1: cfg.parity = UART_PARITY_ODD; break;
        case 2: cfg.parity = UART_PARITY_EVEN; break;
    }

    ESP_LOGI(TAG, "Config: %lu-%d-%d-%d",
             (unsigned long)uart_cfg->baudrate,
             uart_cfg->data_bits, uart_cfg->parity, uart_cfg->stop_bits);

    // Install UART driver
    esp_err_t ret = uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2,
                                        UART_BUF_SIZE * 2, 20, &s_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Driver install failed");
        return ret;
    }

    uart_param_config(UART_PORT_NUM, &cfg);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    s_rx_count = 0;
    s_error_count = 0;
    s_frame_idx = 0;
    s_receiving = false;
    s_running = true;

    xTaskCreate(uart_rx_task, "uart_rx", TASK_STACK_UART,
                NULL, TASK_PRIORITY_UART, &s_task);

    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void uart_handler_stop(void)
{
    if (s_running) {
        ESP_LOGI(TAG, "Stopping...");
        s_running = false;

        if (s_task) {
            vTaskDelay(pdMS_TO_TICKS(200));
            s_task = NULL;
        }

        uart_driver_delete(UART_PORT_NUM);
        s_queue = NULL;
        ESP_LOGI(TAG, "Stopped");
    }
}

bool uart_handler_is_receiving(void)
{
    return s_receiving;
}

uint32_t uart_handler_get_rx_count(void)
{
    return s_rx_count;
}

uint32_t uart_handler_get_error_count(void)
{
    return s_error_count;
}

void uart_handler_set_callback(uart_frame_cb_t cb)
{
    s_callback = cb;
}

esp_err_t uart_handler_update_protocol(const protocol_config_data_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    
    /**
     * 특허 청구항 2: 무중단 컨텍스트 전환
     * "현재 처리 중인 프레임 완료 후 다음 프레임부터 새로운 규칙을 적용"
     * 
     * 구현:
     * 1. 현재 프레임 버퍼 리셋 (진행 중인 불완전 프레임 폐기)
     * 2. 새로운 파싱 규칙 즉시 적용
     * 3. 다음 수신 데이터부터 새 규칙으로 처리
     * 
     * 이를 통해 재부팅 없이 파싱 파이프라인 동적 재구성 가능
     */
    
    // 새 프로토콜 설정 즉시 적용 (런타임 파싱 규칙 관리)
    memcpy(&s_proto_cfg, cfg, sizeof(protocol_config_data_t));
    
    // 상태 기계 동적 재구성: 프레임 버퍼 리셋
    // 현재 수집 중인 불완전한 프레임은 폐기하고
    // 새로운 STX/ETX/길이 설정으로 프레임 탐지 시작
    s_frame_idx = 0;
    
    ESP_LOGI(TAG, "Protocol dynamically updated (no reboot): type=%d", cfg->type);
    ESP_LOGI(TAG, "  - Frame parsing pipeline reconfigured");
    ESP_LOGI(TAG, "  - Next frame will use new settings");
    
    return ESP_OK;
}
