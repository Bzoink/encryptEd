#include "core/utf8.h"

#include <string.h>

static int is_continuation(uint8_t byte)
{
    return byte >= 0x80U && byte <= 0xBFU;
}

static int is_binary_control(uint8_t byte)
{
    return byte < 0x20U && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f';
}

static int is_probably_text_byte(uint8_t byte)
{
    return byte == '\t' || byte == '\n' || byte == '\r' || (byte >= 0x20U && byte <= 0x7EU);
}

static int utf8_bytes_for_codepoint(uint32_t codepoint)
{
    if (codepoint <= 0x7FU) return 1;
    if (codepoint <= 0x7FFU) return 2;
    if (codepoint <= 0xFFFFU) return 3;
    if (codepoint <= 0x10FFFFU) return 4;
    return 0;
}

static int encode_utf8_codepoint(uint32_t codepoint, uint8_t *dst)
{
    int len = utf8_bytes_for_codepoint(codepoint);
    if (len == 1) {
        dst[0] = (uint8_t)codepoint;
    } else if (len == 2) {
        dst[0] = (uint8_t)(0xC0U | (codepoint >> 6));
        dst[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
    } else if (len == 3) {
        dst[0] = (uint8_t)(0xE0U | (codepoint >> 12));
        dst[1] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3FU));
        dst[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
    } else if (len == 4) {
        dst[0] = (uint8_t)(0xF0U | (codepoint >> 18));
        dst[1] = (uint8_t)(0x80U | ((codepoint >> 12) & 0x3FU));
        dst[2] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3FU));
        dst[3] = (uint8_t)(0x80U | (codepoint & 0x3FU));
    }
    return len;
}

static uint32_t cp1252_codepoint(uint8_t byte)
{
    static const uint16_t table[32] = {
        0x20ACU, 0x0081U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
        0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x008DU, 0x017DU, 0x008FU,
        0x0090U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
        0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x009DU, 0x017EU, 0x0178U
    };

    if (byte < 0x80U) return byte;
    if (byte < 0xA0U) return table[byte - 0x80U];
    return byte;
}

static int copy_bytes(const uint8_t *data, size_t len, txx_buffer *buffer)
{
    txx_status status = txx_buffer_alloc(buffer, len);
    if (status != TXX_OK) {
        return status;
    }
    if (len > 0) {
        memcpy(buffer->data, data, len);
    }
    return TXX_OK;
}

static int looks_like_binary_8bit(const uint8_t *data, size_t len)
{
    size_t i;
    size_t controls = 0;

    for (i = 0; i < len; ++i) {
        if (data[i] == 0U) {
            return 1;
        }
        if (is_binary_control(data[i])) {
            ++controls;
        }
    }

    return controls * 20U > len;
}

static txx_text_encoding guess_utf16_without_bom(const uint8_t *data, size_t len)
{
    size_t sample_len;
    size_t i;
    size_t zero_even = 0;
    size_t zero_odd = 0;
    size_t text_even = 0;
    size_t text_odd = 0;
    size_t pairs;

    if (len < 4U) {
        return TXX_TEXT_ENCODING_UNKNOWN;
    }

    sample_len = len < 256U ? len : 256U;
    sample_len -= sample_len % 2U;
    if (sample_len < 4U) {
        return TXX_TEXT_ENCODING_UNKNOWN;
    }

    for (i = 0; i < sample_len; i += 2U) {
        if (data[i] == 0U) {
            ++zero_even;
        } else if (is_probably_text_byte(data[i])) {
            ++text_even;
        }

        if (data[i + 1U] == 0U) {
            ++zero_odd;
        } else if (is_probably_text_byte(data[i + 1U])) {
            ++text_odd;
        }
    }

    pairs = sample_len / 2U;
    if (zero_odd * 3U >= pairs && zero_even == 0U && text_even * 2U >= pairs) {
        return TXX_TEXT_ENCODING_UTF16_LE;
    }
    if (zero_even * 3U >= pairs && zero_odd == 0U && text_odd * 2U >= pairs) {
        return TXX_TEXT_ENCODING_UTF16_BE;
    }
    return TXX_TEXT_ENCODING_UNKNOWN;
}

static txx_text_encoding detect_legacy_8bit_encoding(const uint8_t *data, size_t len)
{
    size_t i;
    int has_high_bytes = 0;

    for (i = 0; i < len; ++i) {
        if (data[i] >= 0x80U && data[i] < 0xA0U) {
            return TXX_TEXT_ENCODING_CP1252;
        }
        if (data[i] >= 0x80U) {
            has_high_bytes = 1;
        }
    }

    if (has_high_bytes) {
        return TXX_TEXT_ENCODING_ISO_8859_1;
    }
    return TXX_TEXT_ENCODING_UNKNOWN;
}

static txx_text_encoding detect_text_encoding(const uint8_t *data, size_t len, int *has_bom)
{
    txx_text_encoding utf16_guess;

    if (has_bom != NULL) {
        *has_bom = 0;
    }

    if (len >= 3U && data[0] == 0xEFU && data[1] == 0xBBU && data[2] == 0xBFU) {
        if (has_bom != NULL) {
            *has_bom = 1;
        }
        return txx_utf8_validate(data + 3U, len - 3U)
            ? TXX_TEXT_ENCODING_UTF8_BOM
            : TXX_TEXT_ENCODING_UNKNOWN;
    }

    if (len >= 2U && data[0] == 0xFFU && data[1] == 0xFEU) {
        if (has_bom != NULL) {
            *has_bom = 1;
        }
        return TXX_TEXT_ENCODING_UTF16_LE;
    }

    if (len >= 2U && data[0] == 0xFEU && data[1] == 0xFFU) {
        if (has_bom != NULL) {
            *has_bom = 1;
        }
        return TXX_TEXT_ENCODING_UTF16_BE;
    }

    utf16_guess = guess_utf16_without_bom(data, len);
    if (utf16_guess != TXX_TEXT_ENCODING_UNKNOWN) {
        return utf16_guess;
    }

    if (looks_like_binary_8bit(data, len)) {
        return TXX_TEXT_ENCODING_UNKNOWN;
    }

    if (txx_utf8_validate(data, len)) {
        return TXX_TEXT_ENCODING_UTF8;
    }

    return detect_legacy_8bit_encoding(data, len);
}

static txx_status decode_utf16(const uint8_t *data,
                               size_t len,
                               txx_text_encoding encoding,
                               int has_bom,
                               txx_buffer *utf8)
{
    size_t start = has_bom ? 2U : 0U;
    size_t i;
    size_t out_len = 0;
    size_t out_pos = 0;

    if (len < start) {
        return TXX_ERR_FORMAT;
    }
    if (((len - start) % 2U) != 0U) {
        return TXX_ERR_FORMAT;
    }

    for (i = start; i < len; i += 2U) {
        uint16_t unit;
        uint32_t codepoint;

        if (encoding == TXX_TEXT_ENCODING_UTF16_LE) {
            unit = (uint16_t)(data[i] | ((uint16_t)data[i + 1U] << 8));
        } else {
            unit = (uint16_t)(((uint16_t)data[i] << 8) | data[i + 1U]);
        }

        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            uint16_t next;
            if (i + 3U >= len) {
                return TXX_ERR_FORMAT;
            }
            if (encoding == TXX_TEXT_ENCODING_UTF16_LE) {
                next = (uint16_t)(data[i + 2U] | ((uint16_t)data[i + 3U] << 8));
            } else {
                next = (uint16_t)(((uint16_t)data[i + 2U] << 8) | data[i + 3U]);
            }
            if (next < 0xDC00U || next > 0xDFFFU) {
                return TXX_ERR_FORMAT;
            }

            codepoint = 0x10000U + ((((uint32_t)unit - 0xD800U) << 10)
                                  | ((uint32_t)next - 0xDC00U));
            i += 2U;
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return TXX_ERR_FORMAT;
        } else {
            codepoint = unit;
        }

        out_len += (size_t)utf8_bytes_for_codepoint(codepoint);
    }

    {
        txx_status status = txx_buffer_alloc(utf8, out_len);
        if (status != TXX_OK) {
            return status;
        }
    }

    for (i = start; i < len; i += 2U) {
        uint16_t unit;
        uint32_t codepoint;

        if (encoding == TXX_TEXT_ENCODING_UTF16_LE) {
            unit = (uint16_t)(data[i] | ((uint16_t)data[i + 1U] << 8));
        } else {
            unit = (uint16_t)(((uint16_t)data[i] << 8) | data[i + 1U]);
        }

        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            uint16_t next;
            if (encoding == TXX_TEXT_ENCODING_UTF16_LE) {
                next = (uint16_t)(data[i + 2U] | ((uint16_t)data[i + 3U] << 8));
            } else {
                next = (uint16_t)(((uint16_t)data[i + 2U] << 8) | data[i + 3U]);
            }
            codepoint = 0x10000U + ((((uint32_t)unit - 0xD800U) << 10)
                                  | ((uint32_t)next - 0xDC00U));
            i += 2U;
        } else {
            codepoint = unit;
        }

        out_pos += (size_t)encode_utf8_codepoint(codepoint, utf8->data + out_pos);
    }

    return TXX_OK;
}

static txx_status decode_8bit(const uint8_t *data,
                              size_t len,
                              txx_text_encoding encoding,
                              txx_buffer *utf8)
{
    size_t i;
    size_t out_len = 0;
    size_t out_pos = 0;

    for (i = 0; i < len; ++i) {
        uint32_t codepoint = encoding == TXX_TEXT_ENCODING_CP1252
            ? cp1252_codepoint(data[i])
            : data[i];
        out_len += (size_t)utf8_bytes_for_codepoint(codepoint);
    }

    {
        txx_status status = txx_buffer_alloc(utf8, out_len);
        if (status != TXX_OK) {
            return status;
        }
    }

    for (i = 0; i < len; ++i) {
        uint32_t codepoint = encoding == TXX_TEXT_ENCODING_CP1252
            ? cp1252_codepoint(data[i])
            : data[i];
        out_pos += (size_t)encode_utf8_codepoint(codepoint, utf8->data + out_pos);
    }

    return TXX_OK;
}

int txx_utf8_validate(const uint8_t *data, size_t len)
{
    size_t i = 0;

    if (data == NULL && len != 0) {
        return 0;
    }

    while (i < len) {
        uint8_t byte = data[i];

        if (byte <= 0x7FU) {
            i += 1;
        } else if (byte >= 0xC2U && byte <= 0xDFU) {
            if (i + 1 >= len || !is_continuation(data[i + 1])) {
                return 0;
            }
            i += 2;
        } else if (byte == 0xE0U) {
            if (i + 2 >= len || data[i + 1] < 0xA0U || data[i + 1] > 0xBFU ||
                !is_continuation(data[i + 2])) {
                return 0;
            }
            i += 3;
        } else if ((byte >= 0xE1U && byte <= 0xECU) || (byte >= 0xEEU && byte <= 0xEFU)) {
            if (i + 2 >= len || !is_continuation(data[i + 1]) || !is_continuation(data[i + 2])) {
                return 0;
            }
            i += 3;
        } else if (byte == 0xEDU) {
            if (i + 2 >= len || data[i + 1] < 0x80U || data[i + 1] > 0x9FU ||
                !is_continuation(data[i + 2])) {
                return 0;
            }
            i += 3;
        } else if (byte == 0xF0U) {
            if (i + 3 >= len || data[i + 1] < 0x90U || data[i + 1] > 0xBFU ||
                !is_continuation(data[i + 2]) || !is_continuation(data[i + 3])) {
                return 0;
            }
            i += 4;
        } else if (byte >= 0xF1U && byte <= 0xF3U) {
            if (i + 3 >= len || !is_continuation(data[i + 1]) || !is_continuation(data[i + 2]) ||
                !is_continuation(data[i + 3])) {
                return 0;
            }
            i += 4;
        } else if (byte == 0xF4U) {
            if (i + 3 >= len || data[i + 1] < 0x80U || data[i + 1] > 0x8FU ||
                !is_continuation(data[i + 2]) || !is_continuation(data[i + 3])) {
                return 0;
            }
            i += 4;
        } else {
            return 0;
        }
    }

    return 1;
}

void txx_text_info_init(txx_text_info *info)
{
    if (info != NULL) {
        info->encoding = TXX_TEXT_ENCODING_UNKNOWN;
        info->newline_style = TXX_NEWLINE_UNKNOWN;
        info->has_bom = 0;
        info->mixed_newlines = 0;
        info->was_transcoded = 0;
    }
}

txx_newline_style txx_text_detect_newlines(const uint8_t *data, size_t len, int *mixed_out)
{
    size_t i;
    int saw_lf = 0;
    int saw_crlf = 0;
    int saw_cr = 0;
    int kinds = 0;

    if (mixed_out != NULL) {
        *mixed_out = 0;
    }

    if (data == NULL && len != 0U) {
        return TXX_NEWLINE_UNKNOWN;
    }

    for (i = 0; i < len; ++i) {
        if (data[i] == '\r') {
            if (i + 1U < len && data[i + 1U] == '\n') {
                saw_crlf = 1;
                ++i;
            } else {
                saw_cr = 1;
            }
        } else if (data[i] == '\n') {
            saw_lf = 1;
        }
    }

    kinds = saw_lf + saw_crlf + saw_cr;
    if (kinds > 1) {
        if (mixed_out != NULL) {
            *mixed_out = 1;
        }
        return TXX_NEWLINE_MIXED;
    }
    if (saw_crlf) return TXX_NEWLINE_CRLF;
    if (saw_lf) return TXX_NEWLINE_LF;
    if (saw_cr) return TXX_NEWLINE_CR;
    return TXX_NEWLINE_UNKNOWN;
}

txx_status txx_text_decode_to_utf8(const uint8_t *data, size_t len, txx_buffer *utf8, txx_text_info *info)
{
    txx_text_info local_info;
    txx_text_encoding encoding;
    int has_bom = 0;
    txx_status status = TXX_OK;

    if ((data == NULL && len != 0U) || utf8 == NULL) {
        return TXX_ERR_ARG;
    }

    txx_buffer_init(utf8);
    txx_text_info_init(&local_info);

    encoding = detect_text_encoding(data, len, &has_bom);
    if (encoding == TXX_TEXT_ENCODING_UNKNOWN) {
        return TXX_ERR_FORMAT;
    }

    local_info.encoding = encoding;
    local_info.has_bom = has_bom;
    local_info.was_transcoded = (encoding == TXX_TEXT_ENCODING_UTF16_LE)
                             || (encoding == TXX_TEXT_ENCODING_UTF16_BE)
                             || (encoding == TXX_TEXT_ENCODING_CP1252)
                             || (encoding == TXX_TEXT_ENCODING_ISO_8859_1);

    if (encoding == TXX_TEXT_ENCODING_UTF8) {
        status = copy_bytes(data, len, utf8);
    } else if (encoding == TXX_TEXT_ENCODING_UTF8_BOM) {
        status = copy_bytes(data + 3U, len - 3U, utf8);
    } else if (encoding == TXX_TEXT_ENCODING_UTF16_LE || encoding == TXX_TEXT_ENCODING_UTF16_BE) {
        status = decode_utf16(data, len, encoding, has_bom, utf8);
    } else {
        status = decode_8bit(data, len, encoding, utf8);
    }
    if (status != TXX_OK) {
        return status;
    }

    if (!txx_utf8_validate(utf8->data, utf8->len)) {
        txx_buffer_free(utf8);
        return TXX_ERR_UTF8;
    }

    local_info.newline_style = txx_text_detect_newlines(utf8->data, utf8->len, &local_info.mixed_newlines);
    if (info != NULL) {
        *info = local_info;
    }
    return TXX_OK;
}

txx_status txx_text_convert_newlines(const uint8_t *data,
                                     size_t len,
                                     txx_newline_style style,
                                     txx_buffer *output)
{
    size_t i;
    size_t out_len = 0;
    size_t out_pos = 0;
    const char *newline = "\n";
    size_t newline_len = 1U;

    if ((data == NULL && len != 0U) || output == NULL) {
        return TXX_ERR_ARG;
    }

    txx_buffer_init(output);

    if (style == TXX_NEWLINE_UNKNOWN || style == TXX_NEWLINE_MIXED) {
        return copy_bytes(data, len, output);
    }
    if (style == TXX_NEWLINE_CRLF) {
        newline = "\r\n";
        newline_len = 2U;
    } else if (style == TXX_NEWLINE_CR) {
        newline = "\r";
    }

    for (i = 0; i < len; ++i) {
        if (data[i] == '\r') {
            out_len += newline_len;
            if (i + 1U < len && data[i + 1U] == '\n') {
                ++i;
            }
        } else if (data[i] == '\n') {
            out_len += newline_len;
        } else {
            out_len += 1U;
        }
    }

    {
        txx_status status = txx_buffer_alloc(output, out_len);
        if (status != TXX_OK) {
            return status;
        }
    }

    for (i = 0; i < len; ++i) {
        if (data[i] == '\r') {
            memcpy(output->data + out_pos, newline, newline_len);
            out_pos += newline_len;
            if (i + 1U < len && data[i + 1U] == '\n') {
                ++i;
            }
        } else if (data[i] == '\n') {
            memcpy(output->data + out_pos, newline, newline_len);
            out_pos += newline_len;
        } else {
            output->data[out_pos++] = data[i];
        }
    }

    return TXX_OK;
}

const char *txx_text_encoding_string(txx_text_encoding encoding)
{
    switch (encoding) {
    case TXX_TEXT_ENCODING_UTF8:
        return "UTF-8";
    case TXX_TEXT_ENCODING_UTF8_BOM:
        return "UTF-8 with BOM";
    case TXX_TEXT_ENCODING_UTF16_LE:
        return "UTF-16 LE";
    case TXX_TEXT_ENCODING_UTF16_BE:
        return "UTF-16 BE";
    case TXX_TEXT_ENCODING_CP1252:
        return "Windows-1252";
    case TXX_TEXT_ENCODING_ISO_8859_1:
        return "ISO-8859-1";
    default:
        return "unknown";
    }
}

const char *txx_newline_style_string(txx_newline_style style)
{
    switch (style) {
    case TXX_NEWLINE_LF:
        return "LF";
    case TXX_NEWLINE_CRLF:
        return "CRLF";
    case TXX_NEWLINE_CR:
        return "CR";
    case TXX_NEWLINE_MIXED:
        return "mixed line endings";
    default:
        return "no line endings";
    }
}

uint32_t utf8_decode_next(const char *src, const char *end, int *byte_len)
{
    if (!src || src >= end) {
        *byte_len = 0;
        return 0xFFFDU;
    }
    const uint8_t b0 = (uint8_t)*src;
    if (b0 < 0x80U) {
        *byte_len = 1;
        return (uint32_t)b0;
    }
    int n;
    uint32_t cp;
    if      (b0 >= 0xC2U && b0 <= 0xDFU) { n = 2; cp = b0 & 0x1FU; }
    else if (b0 >= 0xE0U && b0 <= 0xEFU) { n = 3; cp = b0 & 0x0FU; }
    else if (b0 >= 0xF0U && b0 <= 0xF4U) { n = 4; cp = b0 & 0x07U; }
    else { *byte_len = 1; return 0xFFFDU; }

    if (src + n > end) { *byte_len = 1; return 0xFFFDU; }
    for (int i = 1; i < n; ++i) {
        const uint8_t b = (uint8_t)src[i];
        if (b < 0x80U || b > 0xBFU) { *byte_len = 1; return 0xFFFDU; }
        cp = (cp << 6) | (b & 0x3FU);
    }
    if ((n == 2 && cp < 0x80U) ||
        (n == 3 && (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU))) ||
        (n == 4 && (cp < 0x10000U || cp > 0x10FFFFU))) {
        *byte_len = 1;
        return 0xFFFDU;
    }
    *byte_len = n;
    return cp;
}

int utf8_codepoint_width(uint32_t cp)
{
    /* C0/C1 control characters */
    if (cp < 0x20U || (cp >= 0x7FU && cp < 0xA0U)) return 0;
    /* Soft hyphen, zero-width space, BOM/ZWNBSP */
    if (cp == 0x00ADU || cp == 0x200BU || cp == 0xFEFFU) return 0;
    /* Wide characters (East Asian Width W and F -- simplified ranges) */
    if ((cp >= 0x1100U  && cp <= 0x115FU)  ||
        cp == 0x2329U   || cp == 0x232AU   ||
        (cp >= 0x2E80U  && cp <= 0x303EU)  ||
        (cp >= 0x3040U  && cp <= 0x33FFU)  ||
        (cp >= 0x3400U  && cp <= 0x4DBFU)  ||
        (cp >= 0x4E00U  && cp <= 0xA4CFU)  ||
        (cp >= 0xA960U  && cp <= 0xA97FU)  ||
        (cp >= 0xAC00U  && cp <= 0xD7FFU)  ||
        (cp >= 0xF900U  && cp <= 0xFAFFU)  ||
        (cp >= 0xFE10U  && cp <= 0xFE6FU)  ||
        (cp >= 0xFF01U  && cp <= 0xFF60U)  ||
        (cp >= 0xFFE0U  && cp <= 0xFFE6U)  ||
        (cp >= 0x1B000U && cp <= 0x1B0FFU) ||
        (cp >= 0x1F300U && cp <= 0x1FAFFU) ||
        (cp >= 0x20000U && cp <= 0x2A6DFU) ||
        (cp >= 0x2A700U && cp <= 0x2CEAFU) ||
        (cp >= 0x2CEB0U && cp <= 0x2EBEFU) ||
        (cp >= 0x30000U && cp <= 0x3134FU))
        return 2;
    return 1;
}