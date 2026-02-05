/**
 * @file protocol_def.h
 * @brief RS232 to MQTT Protocol Definitions
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * 통합 인터페이스 정의서 v3.0 기반 정의
 * S0: 보안 강화 (EMQX 인증 필수)
 * S1: MQTT 토픽/QoS 통일, legacy 토픽 제거
 * S2: 중복 코드 제거
 */

#ifndef PROTOCOL_DEF_H
#define PROTOCOL_DEF_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Schema Version (통합 인터페이스 정의서 버전)
 ******************************************************************************/
#define SCHEMA_VERSION_MAJOR    3
#define SCHEMA_VERSION_MINOR    0
#define SCHEMA_VERSION_PATCH    0
#define SCHEMA_VERSION_STRING   "3.0.0"

/*******************************************************************************
 * BLE Service and Characteristic UUIDs
 ******************************************************************************/
#define BLE_SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_WIFI_CONFIG_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_MQTT_CONFIG_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define BLE_CHAR_PROTOCOL_CFG_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define BLE_CHAR_UART_CONFIG_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define BLE_CHAR_DATA_DEF_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26ac"
#define BLE_CHAR_DEVICE_STATUS_UUID "beb5483e-36e1-4688-b7f5-ea07361b26ad"
#define BLE_CHAR_PARSED_DATA_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26ae"
#define BLE_CHAR_COMMAND_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26af"

/*******************************************************************************
 * Packet Structure Constants
 ******************************************************************************/
#define PACKET_STX              0x02
#define PACKET_ETX              0x03
#define PACKET_MAX_PAYLOAD      512
#define PACKET_HEADER_SIZE      4       // STX + CMD + LEN(2)
#define PACKET_FOOTER_SIZE      2       // CRC + ETX (minimum)

/*******************************************************************************
 * Command Codes (Section 3.2)
 ******************************************************************************/
typedef enum {
    CMD_SET_WIFI        = 0x01,
    CMD_SET_MQTT        = 0x02,
    CMD_SET_PROTOCOL    = 0x03,
    CMD_SET_UART        = 0x04,
    CMD_SET_DATA_DEF    = 0x05,
    CMD_GET_STATUS      = 0x06,
    CMD_SAVE_CONFIG     = 0x07,
    CMD_RESET_CONFIG    = 0x08,
    CMD_START_MONITOR   = 0x09,
    CMD_STOP_MONITOR    = 0x0A,
    CMD_REQUEST_SYNC    = 0x0B,     // v2.1: 설정 동기화 요청
    
    // OTA Commands
    CMD_OTA_CHECK       = 0x10,
    CMD_OTA_START       = 0x11,
    CMD_OTA_ABORT       = 0x12,
    CMD_OTA_ROLLBACK    = 0x13,
    CMD_OTA_GET_VERSION = 0x14,
    
    RSP_ACK             = 0x80,
    RSP_STATUS          = 0x81,
    RSP_DATA            = 0x82,
    RSP_OTA_PROGRESS    = 0x83,
    RSP_OTA_VERSION     = 0x84,
    RSP_CONFIG_SYNC     = 0x85,     // v2.1: 설정 동기화 응답
    RSP_ERROR           = 0xFF
} cmd_code_t;

/*******************************************************************************
 * MQTT Remote Command Types (P0-3: 원격 명령 처리)
 ******************************************************************************/
typedef enum {
    MQTT_CMD_UPDATE_CONFIG      = 0x01,     // 설정 업데이트
    MQTT_CMD_RESTART            = 0x02,     // 디바이스 재시작
    MQTT_CMD_REQUEST_STATUS     = 0x03,     // 상태 요청
    MQTT_CMD_START_MONITOR      = 0x04,     // 모니터링 시작
    MQTT_CMD_STOP_MONITOR       = 0x05,     // 모니터링 중지
    MQTT_CMD_FACTORY_RESET      = 0x06,     // 공장 초기화
} mqtt_cmd_type_t;

/*******************************************************************************
 * Config Types for Remote Update (P0-3)
 ******************************************************************************/
typedef enum {
    CONFIG_TYPE_WIFI        = 0x01,
    CONFIG_TYPE_MQTT        = 0x02,
    CONFIG_TYPE_UART        = 0x03,
    CONFIG_TYPE_PROTOCOL    = 0x04,
    CONFIG_TYPE_FIELDS      = 0x05,
    CONFIG_TYPE_ALL         = 0xFF,
} config_type_t;

/*******************************************************************************
 * Protocol Types (Section 1.2)
 ******************************************************************************/
typedef enum {
    PROTOCOL_CUSTOM         = 0x00,
    PROTOCOL_MODBUS_RTU     = 0x01,
    PROTOCOL_MODBUS_ASCII   = 0x02,
    PROTOCOL_NMEA_0183      = 0x03,
    PROTOCOL_IEC_60870_101  = 0x04,
    PROTOCOL_IEC_60870_104  = 0x05
} protocol_type_t;

/*******************************************************************************
 * CRC Types (Section 5.7)
 ******************************************************************************/
typedef enum {
    CRC_NONE            = 0x00,
    CRC_XOR_LRC         = 0x01,
    CRC_SUM8            = 0x02,
    CRC_SUM16           = 0x03,
    CRC_8               = 0x10,
    CRC_8_CCITT         = 0x11,
    CRC_16_IBM          = 0x20,
    CRC_16_CCITT        = 0x21,
    CRC_16_MODBUS       = 0x22,
    CRC_16_XMODEM       = 0x23,
    CRC_32              = 0x30,
    CRC_32_C            = 0x31
} crc_type_t;

/*******************************************************************************
 * Data Type Codes (Section 6.3)
 ******************************************************************************/
typedef enum {
    DATA_TYPE_BOOL          = 0x00,
    DATA_TYPE_UINT8         = 0x01,
    DATA_TYPE_INT8          = 0x02,
    DATA_TYPE_UINT16        = 0x03,
    DATA_TYPE_INT16         = 0x04,
    DATA_TYPE_UINT32        = 0x05,
    DATA_TYPE_INT32         = 0x06,
    DATA_TYPE_UINT64        = 0x07,
    DATA_TYPE_INT64         = 0x08,
    DATA_TYPE_FLOAT32       = 0x10,
    DATA_TYPE_FLOAT64       = 0x11,
    DATA_TYPE_BCD           = 0x20,
    DATA_TYPE_STRING        = 0x30,
    DATA_TYPE_HEX_STRING    = 0x31,
    DATA_TYPE_TIMESTAMP     = 0x40,
    DATA_TYPE_TIMESTAMP_MS  = 0x41
} data_type_t;

/*******************************************************************************
 * Error Codes (Section 7.4)
 ******************************************************************************/
typedef enum {
    ERR_INVALID_COMMAND     = 0x01,
    ERR_INVALID_PARAMETER   = 0x02,
    ERR_CRC_ERROR           = 0x03,
    ERR_BUFFER_OVERFLOW     = 0x04,
    ERR_WIFI_ERROR          = 0x05,
    ERR_MQTT_ERROR          = 0x06,
    ERR_NVS_ERROR           = 0x07,
    ERR_PARSE_ERROR         = 0x08,
    ERR_CONFIG_SYNC_ERROR   = 0x09,     // v2.1: 설정 동기화 에러
    // OTA Errors
    ERR_OTA_WIFI_NOT_CONNECTED = 0x10,
    ERR_OTA_VERSION_CHECK_FAIL = 0x11,
    ERR_OTA_ALREADY_LATEST     = 0x12,
    ERR_OTA_DOWNLOAD_FAILED    = 0x13,
    ERR_OTA_SIGNATURE_INVALID  = 0x14,
    ERR_OTA_FLASH_FAILED       = 0x15,
    ERR_OTA_BUSY               = 0x16,
} error_code_t;

/*******************************************************************************
 * ACK Result Codes (Section 7.1)
 ******************************************************************************/
typedef enum {
    RESULT_SUCCESS  = 0x00,
    RESULT_FAILED   = 0x01,
    RESULT_INVALID  = 0x02
} result_code_t;

/*******************************************************************************
 * Configuration Structures
 ******************************************************************************/

// WiFi Configuration (Section 4.1)
#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASSWORD_MAX_LEN   64

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
} wifi_config_data_t;

// MQTT Configuration (Section 4.2) - P0-1, P0-2 수정
#define MQTT_BROKER_MAX_LEN     128
#define MQTT_USERNAME_MAX_LEN   64
#define MQTT_PASSWORD_MAX_LEN   512     // v2.1: JWT 토큰 지원을 위해 확장
#define MQTT_CLIENT_ID_MAX_LEN  64
#define MQTT_TOPIC_MAX_LEN      128
#define MQTT_USER_ID_MAX_LEN    40      // UUID 형식
#define MQTT_DEVICE_ID_MAX_LEN  50
#define MQTT_BASE_TOPIC_MAX_LEN 128     // v2.1: baseTopic 추가

typedef struct {
    char broker[MQTT_BROKER_MAX_LEN + 1];
    uint16_t port;
    char username[MQTT_USERNAME_MAX_LEN + 1];
    char password[MQTT_PASSWORD_MAX_LEN + 1];   // 비밀번호 또는 JWT 토큰
    char client_id[MQTT_CLIENT_ID_MAX_LEN + 1];
    char topic[MQTT_TOPIC_MAX_LEN + 1];         // Legacy topic (optional)
    
    // v2.1 필수 필드 (P0-1)
    char user_id[MQTT_USER_ID_MAX_LEN + 1];     // ★ SaaS user ID (필수)
    char device_id[MQTT_DEVICE_ID_MAX_LEN + 1]; // ★ Device ID (필수)
    char base_topic[MQTT_BASE_TOPIC_MAX_LEN + 1]; // ★ Base topic (필수)
    
    uint8_t qos;
    bool use_tls;
    bool use_jwt;                               // v2.1: JWT 인증 사용 여부 (P0-2)
} mqtt_config_data_t;

// UART Configuration (Section 4.3)
typedef struct {
    uint32_t baudrate;
    uint8_t data_bits;      // 7 or 8
    uint8_t parity;         // 0=None, 1=Odd, 2=Even
    uint8_t stop_bits;      // 1 or 2
    uint8_t flow_control;   // 0=None, 1=RTS/CTS, 2=XON/XOFF
} uart_config_data_t;

// Custom Protocol Configuration (Section 5.2)
typedef struct {
    uint16_t frame_length;
    bool stx_enable;
    uint16_t stx_value;
    bool etx_enable;
    uint16_t etx_value;
    bool length_field_enable;
    uint8_t length_field_offset;
    uint8_t length_field_size;
    bool length_includes_header;
    crc_type_t crc_type;
    uint16_t crc_offset;
    uint8_t crc_start_offset;
    uint16_t crc_end_offset;
    uint16_t timeout_ms;
} custom_protocol_config_t;

// Modbus RTU Configuration (Section 5.3)
typedef struct {
    uint8_t slave_address;
    uint32_t function_codes;    // Bitmask
    uint16_t inter_frame_delay;
    uint16_t response_timeout;
} modbus_rtu_config_t;

// NMEA Configuration (Section 5.5)
#define NMEA_MAX_FILTERS    8

typedef struct {
    uint8_t sentence_filter_count;
    char sentence_filters[NMEA_MAX_FILTERS][6];
    bool validate_checksum;
    char talker_id_filter[3];
} nmea_config_t;

// IEC 60870-5 Configuration (Section 5.6)
typedef struct {
    uint8_t link_address_size;
    uint8_t asdu_address_size;
    uint8_t ioa_size;
    uint8_t cause_of_tx_size;
    uint8_t originator_address;
    bool balanced_mode;
    uint32_t type_id_filter;
} iec60870_config_t;

// Protocol Configuration Union
typedef struct {
    protocol_type_t type;
    union {
        custom_protocol_config_t custom;
        modbus_rtu_config_t modbus_rtu;
        nmea_config_t nmea;
        iec60870_config_t iec60870;
    } config;
} protocol_config_data_t;

/*******************************************************************************
 * Data Field Definition (Section 6)
 ******************************************************************************/
#define MAX_FIELD_COUNT         64
#define MAX_FIELD_NAME_LEN      32
#define MAX_FIELD_NAMES_SIZE    1024

// Field Definition (12 bytes) - Section 6.2
typedef struct __attribute__((packed)) {
    uint8_t field_type;
    uint8_t byte_order;     // 0=Little, 1=Big Endian
    uint8_t start_offset;
    uint8_t bit_offset;
    uint8_t bit_length;
    uint16_t scale_factor;  // Scale * 1000
    int16_t offset_value;   // Offset * 100
    uint8_t name_length;
    uint16_t name_index;
} field_definition_t;

typedef struct {
    uint8_t field_count;
    uint8_t data_offset;
    field_definition_t fields[MAX_FIELD_COUNT];
    char field_names[MAX_FIELD_NAMES_SIZE];
    uint16_t names_length;
} data_definition_t;

/*******************************************************************************
 * Status Structure (Section 7.2) - v2.1 확장
 ******************************************************************************/
typedef struct __attribute__((packed)) {
    uint8_t wifi_status;        // 0=Disconnected, 1=Connected
    uint8_t mqtt_status;        // 0=Disconnected, 1=Connected
    uint8_t uart_status;        // 0=No data, 1=Receiving
    uint8_t config_status;      // 0=Not configured, 1=Configured
    int8_t rssi;                // WiFi signal strength
    uint32_t uptime;            // Uptime in seconds
    uint32_t rx_count;          // Received frames
    uint32_t tx_count;          // Transmitted messages
    uint32_t error_count;       // CRC/parse errors
    uint32_t firmware_version;  // Version as 0xMMmmPPbb
    uint32_t free_heap;         // v2.1: Free heap size
    char config_hash[9];        // v2.1: 설정 해시 (8자 + null)
} device_status_t;

/*******************************************************************************
 * Config Sync Request/Response (P0-3: 설정 동기화)
 ******************************************************************************/
typedef struct {
    char device_id[MQTT_DEVICE_ID_MAX_LEN + 1];
    char user_id[MQTT_USER_ID_MAX_LEN + 1];
    char current_version[16];
    char config_hash[9];
    uint32_t timestamp;
} config_sync_request_t;

typedef struct {
    bool update_available;
    char latest_version[16];
    uint32_t timestamp;
    // config payload는 별도로 처리
} config_sync_response_t;

/*******************************************************************************
 * MQTT Remote Command (P0-3)
 ******************************************************************************/
typedef struct {
    mqtt_cmd_type_t command;
    uint32_t timestamp;
    char request_id[37];        // UUID
    config_type_t config_type;  // update_config 명령 시 사용
} mqtt_remote_command_t;

/*******************************************************************************
 * Parsed Data Structure (Section 7.3)
 ******************************************************************************/
typedef struct {
    uint32_t timestamp;
    uint16_t sequence;
    uint8_t field_count;
    uint8_t data_format;        // 0=Raw, 1=JSON, 2=Compact
} parsed_data_header_t;

/*******************************************************************************
 * BLE Packet Structure
 ******************************************************************************/
typedef struct {
    uint8_t stx;
    uint8_t cmd;
    uint16_t length;
    uint8_t payload[PACKET_MAX_PAYLOAD];
    uint8_t crc;
    uint8_t etx;
} ble_packet_t;

/*******************************************************************************
 * Parsed Field Value Union
 ******************************************************************************/
typedef union {
    bool b;
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    float f32;
    double f64;
    char str[64];
} field_value_t;

typedef struct {
    char name[MAX_FIELD_NAME_LEN];
    data_type_t type;
    field_value_t value;
    double scaled_value;
    bool crc_valid;             // v2.1: CRC 검증 결과
} parsed_field_t;

/*******************************************************************************
 * System Configuration
 ******************************************************************************/
#define DEVICE_NAME             "RS232_MQTT_Bridge"
#define FIRMWARE_VERSION        0x03000000  // v3.0.0.0

// Default UART settings
#define DEFAULT_BAUDRATE        115200
#define DEFAULT_DATA_BITS       8
#define DEFAULT_PARITY          0
#define DEFAULT_STOP_BITS       1
#define DEFAULT_FLOW_CONTROL    0

// Default MQTT settings
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_PORT_TLS   8883
#define DEFAULT_MQTT_QOS        1           // v3.0: QoS 0→1 변경 (데이터 손실 방지)

// UART Hardware Configuration
#define UART_PORT_NUM           1
#define UART_TX_PIN             17
#define UART_RX_PIN             18
#define UART_RTS_PIN            (-1)
#define UART_CTS_PIN            (-1)
#define UART_BUF_SIZE           1024

// Frame buffer
#define FRAME_BUF_SIZE          512

// Task priorities
#define TASK_PRIORITY_BLE       5
#define TASK_PRIORITY_UART      6
#define TASK_PRIORITY_MQTT      4
#define TASK_PRIORITY_PARSER    5

// Task stack sizes
#define TASK_STACK_BLE          4096
#define TASK_STACK_UART         4096
#define TASK_STACK_MQTT         8192
#define TASK_STACK_PARSER       8192

// Queue sizes
#define UART_RX_QUEUE_SIZE      10
#define PARSED_DATA_QUEUE_SIZE  20
#define BLE_CMD_QUEUE_SIZE      10

/*******************************************************************************
 * Helper Macros
 ******************************************************************************/
// 설정 해시 계산용 (간단한 구현)
#define CONFIG_HASH_LEN         8

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_DEF_H
