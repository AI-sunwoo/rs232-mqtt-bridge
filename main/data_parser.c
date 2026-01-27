/**
 * @file data_parser.c
 * @brief Data Field Parser Implementation
 * 
 * Protocol Spec Section 6 기반 데이터 파싱
 */

#include "data_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "Parser";

static data_definition_t s_def = {0};

esp_err_t data_parser_init(void)
{
    memset(&s_def, 0, sizeof(s_def));
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t data_parser_set_definition(const data_definition_t *def)
{
    if (!def) return ESP_ERR_INVALID_ARG;
    memcpy(&s_def, def, sizeof(data_definition_t));
    ESP_LOGI(TAG, "Definition set: %d fields, offset %d", 
             def->field_count, def->data_offset);
    return ESP_OK;
}

const data_definition_t* data_parser_get_definition(void)
{
    return &s_def;
}

void data_parser_get_field_name(const data_definition_t *def,
                                uint8_t index, char *name, size_t max_len)
{
    if (!def || !name || max_len == 0 || index >= def->field_count) {
        if (name && max_len > 0) name[0] = '\0';
        return;
    }

    const field_definition_t *field = &def->fields[index];
    uint16_t name_idx = field->name_index;

    if (name_idx < def->names_length) {
        size_t len = 0;
        while (name_idx + len < def->names_length &&
               def->field_names[name_idx + len] != '\0' &&
               len < max_len - 1) {
            name[len] = def->field_names[name_idx + len];
            len++;
        }
        name[len] = '\0';
    } else {
        snprintf(name, max_len, "Field%d", index);
    }
}

// 바이트 읽기 (엔디안 처리)
static uint64_t read_bytes(const uint8_t *data, uint8_t offset,
                           uint8_t size, bool big_endian)
{
    uint64_t value = 0;
    if (big_endian) {
        for (uint8_t i = 0; i < size; i++) {
            value = (value << 8) | data[offset + i];
        }
    } else {
        for (uint8_t i = 0; i < size; i++) {
            value |= ((uint64_t)data[offset + i]) << (i * 8);
        }
    }
    return value;
}

// 스케일링 적용
static double apply_scale(double raw, const field_definition_t *field)
{
    double scale = field->scale_factor / 1000.0;
    double offset = field->offset_value / 100.0;
    if (scale == 0) scale = 1.0;
    return (raw * scale) + offset;
}

int data_parser_parse_frame(const uint8_t *raw_data, size_t raw_len,
                            parsed_field_t *fields, uint8_t max_fields)
{
    if (!raw_data || !fields || s_def.field_count == 0) {
        return -1;
    }

    if (s_def.data_offset >= raw_len) {
        ESP_LOGW(TAG, "Data offset beyond frame");
        return -1;
    }

    const uint8_t *data = raw_data + s_def.data_offset;
    size_t data_len = raw_len - s_def.data_offset;

    uint8_t count = (s_def.field_count < max_fields) ? 
                    s_def.field_count : max_fields;

    for (uint8_t i = 0; i < count; i++) {
        const field_definition_t *fd = &s_def.fields[i];
        parsed_field_t *out = &fields[i];

        data_parser_get_field_name(&s_def, i, out->name, MAX_FIELD_NAME_LEN);
        out->type = (data_type_t)fd->field_type;

        if (fd->start_offset >= data_len) {
            ESP_LOGW(TAG, "Field %d offset out of bounds", i);
            continue;
        }

        bool big_endian = (fd->byte_order != 0);
        double raw_val = 0;

        switch (fd->field_type) {
            case DATA_TYPE_BOOL: {
                uint8_t byte = data[fd->start_offset];
                out->value.b = (byte >> fd->bit_offset) & 0x01;
                raw_val = out->value.b ? 1.0 : 0.0;
                break;
            }

            case DATA_TYPE_UINT8:
                out->value.u8 = data[fd->start_offset];
                raw_val = out->value.u8;
                break;

            case DATA_TYPE_INT8:
                out->value.i8 = (int8_t)data[fd->start_offset];
                raw_val = out->value.i8;
                break;

            case DATA_TYPE_UINT16:
                out->value.u16 = (uint16_t)read_bytes(data, fd->start_offset, 2, big_endian);
                raw_val = out->value.u16;
                break;

            case DATA_TYPE_INT16:
                out->value.i16 = (int16_t)read_bytes(data, fd->start_offset, 2, big_endian);
                raw_val = out->value.i16;
                break;

            case DATA_TYPE_UINT32:
                out->value.u32 = (uint32_t)read_bytes(data, fd->start_offset, 4, big_endian);
                raw_val = out->value.u32;
                break;

            case DATA_TYPE_INT32:
                out->value.i32 = (int32_t)read_bytes(data, fd->start_offset, 4, big_endian);
                raw_val = out->value.i32;
                break;

            case DATA_TYPE_UINT64:
                out->value.u64 = read_bytes(data, fd->start_offset, 8, big_endian);
                raw_val = (double)out->value.u64;
                break;

            case DATA_TYPE_INT64:
                out->value.i64 = (int64_t)read_bytes(data, fd->start_offset, 8, big_endian);
                raw_val = (double)out->value.i64;
                break;

            case DATA_TYPE_FLOAT32: {
                uint32_t bits = (uint32_t)read_bytes(data, fd->start_offset, 4, big_endian);
                memcpy(&out->value.f32, &bits, sizeof(float));
                raw_val = out->value.f32;
                break;
            }

            case DATA_TYPE_FLOAT64: {
                uint64_t bits = read_bytes(data, fd->start_offset, 8, big_endian);
                memcpy(&out->value.f64, &bits, sizeof(double));
                raw_val = out->value.f64;
                break;
            }

            case DATA_TYPE_BCD: {
                uint64_t result = 0;
                uint8_t byte_len = (fd->bit_length + 7) / 8;
                for (uint8_t j = 0; j < byte_len && fd->start_offset + j < data_len; j++) {
                    uint8_t byte = data[fd->start_offset + j];
                    result = result * 100 + ((byte >> 4) * 10 + (byte & 0x0F));
                }
                out->value.u64 = result;
                raw_val = (double)result;
                break;
            }

            case DATA_TYPE_STRING: {
                uint8_t str_len = fd->bit_length / 8;
                if (str_len > 63) str_len = 63;
                if (fd->start_offset + str_len > data_len) {
                    str_len = data_len - fd->start_offset;
                }
                memcpy(out->value.str, &data[fd->start_offset], str_len);
                out->value.str[str_len] = '\0';
                raw_val = 0;
                break;
            }

            case DATA_TYPE_HEX_STRING: {
                uint8_t byte_len = fd->bit_length / 8;
                if (byte_len > 31) byte_len = 31;
                for (uint8_t j = 0; j < byte_len && fd->start_offset + j < data_len; j++) {
                    sprintf(&out->value.str[j * 2], "%02X", data[fd->start_offset + j]);
                }
                out->value.str[byte_len * 2] = '\0';
                raw_val = 0;
                break;
            }

            case DATA_TYPE_TIMESTAMP:
                out->value.u32 = (uint32_t)read_bytes(data, fd->start_offset, 4, big_endian);
                raw_val = out->value.u32;
                break;

            case DATA_TYPE_TIMESTAMP_MS:
                out->value.u64 = read_bytes(data, fd->start_offset, 8, big_endian);
                raw_val = (double)out->value.u64;
                break;

            default:
                ESP_LOGW(TAG, "Unknown type: 0x%02X", fd->field_type);
                raw_val = 0;
                break;
        }

        // 스케일링 (문자열 제외)
        if (fd->field_type != DATA_TYPE_STRING && 
            fd->field_type != DATA_TYPE_HEX_STRING) {
            out->scaled_value = apply_scale(raw_val, fd);
        } else {
            out->scaled_value = 0;
        }

        ESP_LOGD(TAG, "[%d] %s: %.2f", i, out->name, out->scaled_value);
    }

    return count;
}
