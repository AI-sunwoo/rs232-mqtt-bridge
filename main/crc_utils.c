/**
 * @file crc_utils.c
 * @brief CRC Calculation Utilities Implementation
 */

#include "crc_utils.h"

// CRC-8 다항식: x^8 + x^2 + x + 1 (0x07)
uint8_t crc_calc_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// CRC-8-CCITT 다항식: 0x8D, 초기값: 0x00
uint8_t crc_calc_crc8_ccitt(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x8D;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// XOR/LRC
uint8_t crc_calc_xor(const uint8_t *data, size_t len)
{
    uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result ^= data[i];
    }
    return result;
}

// Sum8
uint8_t crc_calc_sum8(const uint8_t *data, size_t len)
{
    uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result += data[i];
    }
    return result;
}

// Sum16
uint16_t crc_calc_sum16(const uint8_t *data, size_t len)
{
    uint16_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result += data[i];
    }
    return result;
}

// CRC-16-IBM: 다항식 0x8005, 초기값 0x0000, Reflected
uint16_t crc_calc_crc16_ibm(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;  // Reflected polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// CRC-16-CCITT: 다항식 0x1021, 초기값 0xFFFF
uint16_t crc_calc_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// CRC-16-Modbus: 다항식 0x8005, 초기값 0xFFFF, Reflected
uint16_t crc_calc_crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// CRC-16-XMODEM: 다항식 0x1021, 초기값 0x0000
uint16_t crc_calc_crc16_xmodem(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// CRC-32: 다항식 0x04C11DB7, 초기값 0xFFFFFFFF, Reflected
uint32_t crc_calc_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x00000001) {
                crc = (crc >> 1) ^ 0xEDB88320;  // Reflected polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// CRC-32-C (Castagnoli): 다항식 0x1EDC6F41, 초기값 0xFFFFFFFF
uint32_t crc_calc_crc32c(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x00000001) {
                crc = (crc >> 1) ^ 0x82F63B78;  // Reflected polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// CRC 타입에 따른 통합 계산
uint32_t crc_calculate(crc_type_t type, const uint8_t *data, size_t len)
{
    switch (type) {
        case CRC_NONE:
            return 0;
        case CRC_XOR_LRC:
            return crc_calc_xor(data, len);
        case CRC_SUM8:
            return crc_calc_sum8(data, len);
        case CRC_SUM16:
            return crc_calc_sum16(data, len);
        case CRC_8:
            return crc_calc_crc8(data, len);
        case CRC_8_CCITT:
            return crc_calc_crc8_ccitt(data, len);
        case CRC_16_IBM:
            return crc_calc_crc16_ibm(data, len);
        case CRC_16_CCITT:
            return crc_calc_crc16_ccitt(data, len);
        case CRC_16_MODBUS:
            return crc_calc_crc16_modbus(data, len);
        case CRC_16_XMODEM:
            return crc_calc_crc16_xmodem(data, len);
        case CRC_32:
            return crc_calc_crc32(data, len);
        case CRC_32_C:
            return crc_calc_crc32c(data, len);
        default:
            return 0;
    }
}

// CRC 크기 반환
uint8_t crc_get_size(crc_type_t type)
{
    switch (type) {
        case CRC_NONE:
            return 0;
        case CRC_XOR_LRC:
        case CRC_SUM8:
        case CRC_8:
        case CRC_8_CCITT:
            return 1;
        case CRC_SUM16:
        case CRC_16_IBM:
        case CRC_16_CCITT:
        case CRC_16_MODBUS:
        case CRC_16_XMODEM:
            return 2;
        case CRC_32:
        case CRC_32_C:
            return 4;
        default:
            return 0;
    }
}
