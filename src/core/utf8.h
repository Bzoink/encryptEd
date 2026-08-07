#ifndef TXX_UTF8_H
#define TXX_UTF8_H

#include <stddef.h>
#include <stdint.h>

#include "core/txx_buffer.h"

typedef enum txx_text_encoding {
	TXX_TEXT_ENCODING_UNKNOWN = 0,
	TXX_TEXT_ENCODING_UTF8,
	TXX_TEXT_ENCODING_UTF8_BOM,
	TXX_TEXT_ENCODING_UTF16_LE,
	TXX_TEXT_ENCODING_UTF16_BE,
	TXX_TEXT_ENCODING_CP1252,
	TXX_TEXT_ENCODING_ISO_8859_1
} txx_text_encoding;

typedef enum txx_newline_style {
	TXX_NEWLINE_UNKNOWN = 0,
	TXX_NEWLINE_LF,
	TXX_NEWLINE_CRLF,
	TXX_NEWLINE_CR,
	TXX_NEWLINE_MIXED
} txx_newline_style;

typedef struct txx_text_info {
	txx_text_encoding encoding;
	txx_newline_style newline_style;
	int has_bom;
	int mixed_newlines;
	int was_transcoded;
} txx_text_info;

int txx_utf8_validate(const uint8_t *data, size_t len);
void txx_text_info_init(txx_text_info *info);
txx_newline_style txx_text_detect_newlines(const uint8_t *data, size_t len, int *mixed_out);
txx_status txx_text_decode_to_utf8(const uint8_t *data, size_t len, txx_buffer *utf8, txx_text_info *info);
txx_status txx_text_convert_newlines(const uint8_t *data, size_t len, txx_newline_style style, txx_buffer *output);
const char *txx_text_encoding_string(txx_text_encoding encoding);
const char *txx_newline_style_string(txx_newline_style style);

/* Decode one UTF-8 sequence starting at src up to (but not including) end.
   Returns the Unicode code point (0xFFFD on any decoding error).
   Sets *byte_len to the number of bytes consumed; always >= 1 when src < end. */
uint32_t utf8_decode_next(const char *src, const char *end, int *byte_len);

/* Returns the display column width of a Unicode code point:
   0 for control/combining characters, 2 for wide (CJK/emoji), 1 otherwise.
   Does not treat tab or newline specially; callers must handle those. */
int utf8_codepoint_width(uint32_t codepoint);

#endif