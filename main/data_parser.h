/**
 * @file data_parser.h
 * @brief Data Field Parser
 */

#ifndef DATA_PARSER_H
#define DATA_PARSER_H

#include "protocol_def.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 데이터 파서 초기화
 */
esp_err_t data_parser_init(void);

/**
 * @brief 필드 정의 설정
 */
esp_err_t data_parser_set_definition(const data_definition_t *def);

/**
 * @brief 현재 필드 정의 반환
 */
const data_definition_t* data_parser_get_definition(void);

/**
 * @brief 프레임 데이터 파싱 (Section 6)
 * @param raw_data 원시 프레임 데이터
 * @param raw_len 데이터 길이
 * @param fields 파싱 결과 배열
 * @param max_fields 최대 필드 수
 * @return 파싱된 필드 수, 에러시 -1
 */
int data_parser_parse_frame(const uint8_t *raw_data, size_t raw_len,
                            parsed_field_t *fields, uint8_t max_fields);

/**
 * @brief 필드 이름 반환
 */
void data_parser_get_field_name(const data_definition_t *def,
                                uint8_t index, char *name, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // DATA_PARSER_H
