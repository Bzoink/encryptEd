#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/txx_codec.h"
#include "core/txx_compress.h"
#include "core/txx_format.h"
#include "core/utf8.h"
#include "platform/random.h"
#include "platform/secure_mem.h"

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++failures; \
        } \
    } while (0)

static void test_options(txx_codec_options *options)
{
    txx_codec_default_options(options);
    options->compression_threshold = 64U;
    options->compression_min_savings = 1U;
    options->kdf.memory_blocks = TXX_DECODE_MIN_KDF_MEMORY_BLOCKS;
    options->kdf.passes = 2U;
    options->kdf.lanes = 1U;
}

static void test_repeat_empty_password(void)
{
    txx_codec_options options;
    txx_buffer encoded;
    txx_buffer decoded;
    const char *text = "secret";

    test_options(&options);
    CHECK(txx_encode_text((const uint8_t *)text, strlen(text), "", &options, &encoded) ==
          TXX_ERR_EMPTY_PASSWORD);
    CHECK(txx_encode_text((const uint8_t *)text, strlen(text), "pw", &options, &encoded) == TXX_OK);
    CHECK(txx_decode_text(encoded.data, encoded.len, "", &decoded) == TXX_ERR_EMPTY_PASSWORD);
    txx_buffer_secure_free(&encoded);
}

static void test_repeat_weak_kdf_rejected(void)
{
    txx_codec_options options;
    txx_buffer encoded;
    const char *text = "secret";

    test_options(&options);
    options.kdf.memory_blocks = TXX_DECODE_MIN_KDF_MEMORY_BLOCKS - 1U;
    CHECK(txx_encode_text((const uint8_t *)text, strlen(text), "pw", &options, &encoded) ==
          TXX_ERR_UNSUPPORTED);
    txx_buffer_secure_free(&encoded);
}

static void test_repeat_decode_weak_kdf_rejected(void)
{
    const uint8_t text[] = "secret";
    txx_codec_options options;
    txx_buffer encoded;
    txx_buffer decoded;
    txx_header header;

    test_options(&options);
    CHECK(txx_encode_text(text, sizeof(text) - 1U, "right", &options, &encoded) == TXX_OK);
    CHECK(txx_header_decode(encoded.data, &header) == TXX_OK);

    /* A hostile header can request as little as 256 blocks (256 KiB) of Argon2
     * memory; decoding must refuse anything below the documented 8192-block
     * floor before doing any password work. */
    header.kdf_memory_blocks = TXX_DECODE_MIN_KDF_MEMORY_BLOCKS - 1U;
    txx_header_encode(encoded.data, &header);
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_UNSUPPORTED);

    header.kdf_memory_blocks = TXX_MIN_KDF_MEMORY_BLOCKS;
    txx_header_encode(encoded.data, &header);
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_UNSUPPORTED);

    txx_buffer_secure_free(&encoded);
}

static void round_trip_bytes(const uint8_t *text, size_t len)
{
    txx_codec_options options;
    txx_buffer encoded;
    txx_buffer decoded;
    txx_status status;

    test_options(&options);
    status = txx_encode_text(text, len, "correct horse battery staple", &options, &encoded);
    CHECK(status == TXX_OK);
    if (status != TXX_OK) {
        return;
    }

    status = txx_decode_text(encoded.data, encoded.len, "correct horse battery staple", &decoded);
    CHECK(status == TXX_OK);
    if (status == TXX_OK) {
        CHECK(decoded.len == len);
        CHECK(len == 0 || memcmp(decoded.data, text, len) == 0);
        txx_buffer_secure_free(&decoded);
    }
    txx_buffer_secure_free(&encoded);
}

static void test_round_trips(void)
{
    const uint8_t empty[] = "";
    const uint8_t small[] = "hello\n";
    const uint8_t unicode[] = "Hej \xE2\x82\xAC \xF0\x9F\x94\x90\n";
    uint8_t large[4096];
    size_t i;

    for (i = 0; i < sizeof(large); ++i) {
        large[i] = (uint8_t)("abcd\n"[i % 5U]);
    }

    round_trip_bytes(empty, 0);
    round_trip_bytes(small, sizeof(small) - 1U);
    round_trip_bytes(unicode, sizeof(unicode) - 1U);
    round_trip_bytes(large, sizeof(large));
}

static void test_invalid_utf8_rejected(void)
{
    const uint8_t invalid[] = { 0xC0U, 0xAFU };
    txx_codec_options options;
    txx_buffer encoded;

    test_options(&options);
    CHECK(!txx_utf8_validate(invalid, sizeof(invalid)));
    CHECK(txx_encode_text(invalid, sizeof(invalid), "password", &options, &encoded) == TXX_ERR_UTF8);
}

static void check_text_decode(const uint8_t *input,
                              size_t input_len,
                              const char *expected,
                              txx_text_encoding encoding,
                              txx_newline_style newline_style,
                              int mixed,
                              int transcoded)
{
    txx_buffer decoded;
    txx_text_info info;

    txx_buffer_init(&decoded);
    txx_text_info_init(&info);
    CHECK(txx_text_decode_to_utf8(input, input_len, &decoded, &info) == TXX_OK);
    CHECK(info.encoding == encoding);
    CHECK(info.newline_style == newline_style);
    CHECK(info.mixed_newlines == mixed);
    CHECK(info.was_transcoded == transcoded);
    CHECK(decoded.len == strlen(expected));
    CHECK(memcmp(decoded.data, expected, decoded.len) == 0);
    txx_buffer_free(&decoded);
}

static void test_text_decode_variants(void)
{
    static const uint8_t utf8_bom[] = { 0xEFU, 0xBBU, 0xBFU, 'A', '\r', '\n', 'B', '\n' };
    static const uint8_t utf16_le[] = {
        0xFFU, 0xFEU,
        'A', 0x00U,
        '\r', 0x00U,
        'B', 0x00U
    };
    static const uint8_t utf16_be[] = {
        0xFEU, 0xFFU,
        0x00U, 'A',
        0x00U, '\n',
        0x00U, 'B'
    };
    static const uint8_t cp1252[] = { 0x93U, 'H', 'e', 'j', 0x94U, '\r', '\n' };
    static const uint8_t iso88591[] = { 'H', 0xE5U, 'l', 'l', 'o', '\r', '\n' };
    static const uint8_t utf16_le_nobom[] = { 'A', 0x00U, '\r', 0x00U, 'B', 0x00U };

    check_text_decode(utf8_bom, sizeof(utf8_bom), "A\r\nB\n",
                      TXX_TEXT_ENCODING_UTF8_BOM, TXX_NEWLINE_MIXED, 1, 0);
    check_text_decode(utf16_le, sizeof(utf16_le), "A\rB",
                      TXX_TEXT_ENCODING_UTF16_LE, TXX_NEWLINE_CR, 0, 1);
    check_text_decode(utf16_be, sizeof(utf16_be), "A\nB",
                      TXX_TEXT_ENCODING_UTF16_BE, TXX_NEWLINE_LF, 0, 1);
    check_text_decode(cp1252, sizeof(cp1252), "\xE2\x80\x9CHej\xE2\x80\x9D\r\n",
                      TXX_TEXT_ENCODING_CP1252, TXX_NEWLINE_CRLF, 0, 1);
    check_text_decode(iso88591, sizeof(iso88591), "H\xC3\xA5llo\r\n",
                      TXX_TEXT_ENCODING_ISO_8859_1, TXX_NEWLINE_CRLF, 0, 1);
    check_text_decode(utf16_le_nobom, sizeof(utf16_le_nobom), "A\rB",
                      TXX_TEXT_ENCODING_UTF16_LE, TXX_NEWLINE_CR, 0, 1);
}

static void test_text_decode_rejects_binary(void)
{
    static const uint8_t binaryish[] = { 0x00U, 0x01U, 0x02U, 0x03U };
    txx_buffer decoded;
    txx_text_info info;

    txx_buffer_init(&decoded);
    txx_text_info_init(&info);
    CHECK(txx_text_decode_to_utf8(binaryish, sizeof(binaryish), &decoded, &info) == TXX_ERR_FORMAT);
}

static void test_newline_conversion(void)
{
    static const uint8_t mixed[] = "A\r\nB\nC\rD";
    txx_buffer converted;
    int mixed_flag = 0;

    txx_buffer_init(&converted);
    CHECK(txx_text_detect_newlines(mixed, strlen((const char *)mixed), &mixed_flag) == TXX_NEWLINE_MIXED);
    CHECK(mixed_flag == 1);

    CHECK(txx_text_convert_newlines(mixed, strlen((const char *)mixed), TXX_NEWLINE_CRLF, &converted) == TXX_OK);
    CHECK(converted.len == strlen("A\r\nB\r\nC\r\nD"));
    CHECK(memcmp(converted.data, "A\r\nB\r\nC\r\nD", converted.len) == 0);
    txx_buffer_free(&converted);
}

static void test_wrong_password_and_corruption(void)
{
    const uint8_t text[] = "secret document\n";
    txx_codec_options options;
    txx_buffer encoded;
    txx_buffer decoded;
    uint8_t saved;

    test_options(&options);
    CHECK(txx_encode_text(text, sizeof(text) - 1U, "right", &options, &encoded) == TXX_OK);
    CHECK(txx_decode_text(encoded.data, encoded.len, "wrong", &decoded) == TXX_ERR_AUTH);

    saved = encoded.data[0];
    encoded.data[0] ^= 0xffU;
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_FORMAT);
    encoded.data[0] = saved;

    encoded.data[8] = 2U;
    encoded.data[9] = 0U;
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_UNSUPPORTED);
    encoded.data[8] = 1U;

    encoded.data[encoded.len - 1U] ^= 0x55U;
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_AUTH);
    encoded.data[encoded.len - 1U] ^= 0x55U;

    CHECK(txx_decode_text(encoded.data, encoded.len - 1U, "right", &decoded) == TXX_ERR_FORMAT);
    txx_buffer_secure_free(&encoded);
}

static void test_oversized_header_limits_rejected(void)
{
    const uint8_t text[] = "bounded\n";
    txx_codec_options options;
    txx_buffer encoded;
    txx_buffer decoded;
    txx_header header;

    test_options(&options);
    CHECK(txx_encode_text(text, sizeof(text) - 1U, "right", &options, &encoded) == TXX_OK);
    CHECK(txx_header_decode(encoded.data, &header) == TXX_OK);

    header.kdf_memory_blocks = TXX_MAX_KDF_MEMORY_BLOCKS + 1U;
    txx_header_encode(encoded.data, &header);
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_UNSUPPORTED);

    CHECK(txx_header_decode(encoded.data, &header) == TXX_OK);
    header.payload_len = TXX_MAX_TEXT_BYTES + 1U;
    header.ciphertext_len = header.payload_len;
    header.plaintext_len = header.payload_len;
    txx_header_encode(encoded.data, &header);
    CHECK(txx_decode_text(encoded.data, encoded.len, "right", &decoded) == TXX_ERR_UNSUPPORTED);

    txx_buffer_secure_free(&encoded);
}

static void test_oversized_encode_limits_rejected(void)
{
    const uint8_t text[] = "bounded\n";
    txx_codec_options options;
    txx_buffer encoded;

    test_options(&options);
    options.kdf.memory_blocks = TXX_MAX_KDF_MEMORY_BLOCKS + 1U;
    CHECK(txx_encode_text(text, sizeof(text) - 1U, "right", &options, &encoded) == TXX_ERR_UNSUPPORTED);
    CHECK(txx_encode_text(text, (size_t)TXX_MAX_TEXT_BYTES + 1U, "right", NULL, &encoded) == TXX_ERR_OVERFLOW);
}

static void test_compression_decisions(void)
{
    const uint8_t small[] = "abc";
    uint8_t large[2048];
    txx_buffer payload;
    int is_compressed = 0;
    size_t i;

    for (i = 0; i < sizeof(large); ++i) {
        large[i] = (uint8_t)'A';
    }

    CHECK(txx_compress_encode(small, sizeof(small) - 1U, 64U, 1U, &payload, &is_compressed) == TXX_OK);
    CHECK(is_compressed == 0);
    txx_buffer_secure_free(&payload);

    CHECK(txx_compress_encode(large, sizeof(large), 64U, 1U, &payload, &is_compressed) == TXX_OK);
    CHECK(is_compressed == 1);
    CHECK(payload.len < sizeof(large));
    txx_buffer_secure_free(&payload);
}

static void test_header_reports_compression(void)
{
    txx_codec_options options;
    txx_buffer encoded;
    txx_header header;
    uint8_t large[2048];
    size_t i;

    for (i = 0; i < sizeof(large); ++i) {
        large[i] = (uint8_t)'Z';
    }
    test_options(&options);
    CHECK(txx_encode_text(large, sizeof(large), "password", &options, &encoded) == TXX_OK);
    CHECK(txx_header_decode(encoded.data, &header) == TXX_OK);
    CHECK((header.flags & TXX_FLAG_COMPRESSED) != 0);
    CHECK(header.compress_alg == TXX_COMPRESS_MICROZ_DEFLATE);
    txx_buffer_secure_free(&encoded);
}

static void test_platform_helpers(void)
{
    uint8_t a[32];
    uint8_t b[32];
    uint8_t wipe[16];
    int any_nonzero = 0;
    size_t i;

    CHECK(txx_random_bytes(a, sizeof(a)) == 0);
    CHECK(txx_random_bytes(b, sizeof(b)) == 0);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    for (i = 0; i < sizeof(a); ++i) {
        if (a[i] != 0 || b[i] != 0) {
            any_nonzero = 1;
        }
    }
    CHECK(any_nonzero);

    memset(wipe, 0xaa, sizeof(wipe));
    txx_secure_zero(wipe, sizeof(wipe));
    for (i = 0; i < sizeof(wipe); ++i) {
        CHECK(wipe[i] == 0);
    }
}

int main(void)
{
    test_round_trips();
    test_invalid_utf8_rejected();
    test_text_decode_variants();
    test_text_decode_rejects_binary();
    test_newline_conversion();
    test_wrong_password_and_corruption();
    test_oversized_header_limits_rejected();
    test_oversized_encode_limits_rejected();
    test_compression_decisions();
    test_header_reports_compression();
    test_platform_helpers();
    test_repeat_empty_password();
    test_repeat_weak_kdf_rejected();
    test_repeat_decode_weak_kdf_rejected();

    if (failures != 0) {
        fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }
    return 0;
}