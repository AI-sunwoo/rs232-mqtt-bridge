/**
 * @file crc_utils.h
 * @brief CRC Calculation Utilities
 */

#ifndef CRC_UTILS_H
#define CRC_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief XOR/LRC 계산
 */
uint8_t crc_calc_xor(const uint8_t *data, size_t len);

/**
 * @brief Sum8 체크섬 계산
 */
uint8_t crc_calc_sum8(const uint8_t *data, size_t len);

/**
 * @brief Sum16 체크섬 계산
 */
uint16_t crc_calc_sum16(const uint8_t *data, size_t len);

/**
 * @brief CRC-8 계산
 */
uint8_t crc_calc_crc8(const uint8_t *data, size_t len);

/**
 * @brief CRC-8-CCITT 계산
 */
uint8_t crc_calc_crc8_ccitt(const uint8_t *data, size_t len);

/**
 * @brief CRC-16-IBM 계산
 */
uint16_t crc_calc_crc16_ibm(const uint8_t *data, size_t len);

/**
 * @brief CRC-16-CCITT 계산
 */
uint16_t crc_calc_crc16_ccitt(const uint8_t *data, size_t len);

/**
 * @brief CRC-16-Modbus 계산
 */
uint16_t crc_calc_crc16_modbus(const uint8_t *data, size_t len);

/**
 * @brief CRC-16-XMODEM 계산
 */
uint16_t crc_calc_crc16_xmodem(const uint8_t *data, size_t len);

/**
 * @brief CRC-32 계산
 */
uint32_t crc_calc_crc32(const uint8_t *data, size_t len);

/**
 * @brief CRC-32-C 계산
 */
uint32_t crc_calc_crc32c(const uint8_t *data, size_t len);

/**
 * @brief CRC 타입에 따른 계산
 * @param type CRC 타입
 * @param data 데이터 포인터
 * @param len 데이터 길이
 * @return CRC 값 (최대 32비트)
 */
uint32_t crc_calculate(crc_type_t type, const uint8_t *data, size_t len);

/**
 * @brief CRC 크기 반환
 * @param type CRC 타입
 * @return CRC 바이트 크기 (0, 1, 2, 4)
 */
uint8_t crc_get_size(crc_type_t type);

#ifdef __cplusplus
}
#endif

#endif // CRC_UTILS_H
