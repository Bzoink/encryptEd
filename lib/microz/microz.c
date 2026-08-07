/*
 * miniz - DEFLATE compression/decompression
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Adapted for encryptEd: stripped to raw DEFLATE (no ZIP/archive APIs),
 * ported to C17, bp_types replaced with <stdint.h>.
 */

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

#define MINIZ_HAS_64BIT_REGISTERS 0

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define MINIZ_EXPORT

typedef uint8_t       mz_uint8;
typedef int16_t       mz_int16;
typedef uint16_t      mz_uint16;
typedef uint32_t      mz_uint32;
typedef uint32_t      mz_uint;
typedef int64_t       mz_int64;
typedef uint64_t      mz_uint64;
typedef bool          mz_bool;
typedef unsigned long mz_ulong;

_Static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
_Static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");

#define MZ_FALSE false
#define MZ_TRUE  true

#define MZ_MACRO_END while (0)


#define MZ_FILE void *


typedef struct mz_dummy_time_t_tag {
    mz_uint32 m_dummy1;
    mz_uint32 m_dummy2;
} mz_dummy_time_t;
#define MZ_TIME_T mz_dummy_time_t

#define MZ_ASSERT(x) assert(x)

#ifdef MINIZ_NO_MALLOC
#define MZ_MALLOC(x) NULL
#define MZ_FREE(x) (void)x, ((void)0)
#define MZ_REALLOC(p, x) NULL
#else
#define MZ_MALLOC(x) malloc(x)
#define MZ_FREE(x) free(x)
#define MZ_REALLOC(p, x) realloc(p, x)
#endif

#define MZ_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MZ_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MZ_CLEAR_OBJ(obj) memset(&(obj), 0, sizeof(obj))
#define MZ_CLEAR_ARR(obj) memset((obj), 0, sizeof(obj))
#define MZ_CLEAR_PTR(obj) memset((obj), 0, sizeof(*obj))

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
#define MZ_READ_LE16(p) *((const mz_uint16 *)(p))
#define MZ_READ_LE32(p) *((const mz_uint32 *)(p))
#else
#define MZ_READ_LE16(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8U))
#define MZ_READ_LE32(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8U) | ((mz_uint32)(((const mz_uint8 *)(p))[2]) << 16U) | ((mz_uint32)(((const mz_uint8 *)(p))[3]) << 24U))
#endif


#ifdef __cplusplus
extern "C"
{
#endif

MINIZ_EXPORT void *miniz_def_alloc_func(void *opaque, size_t items, size_t size);
MINIZ_EXPORT void  miniz_def_free_func(void *opaque, void *address);
MINIZ_EXPORT void *miniz_def_realloc_func(void *opaque, void *address, size_t items, size_t size);

#define MZ_UINT16_MAX (0xFFFFU)
#define MZ_UINT32_MAX (0xFFFFFFFFU)

#ifdef __cplusplus
}
#endif


#if defined(__GNUC__) || defined(__clang__)
#define MZ_FORCEINLINE inline __attribute__((__always_inline__))
#else
#define MZ_FORCEINLINE inline
#endif


#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__i386) || \
    defined(__i486__) || defined(__i486) || defined(i386) || \
    defined(__ia64__) || defined(__x86_64__)
#define MINIZ_X86_OR_X64_CPU 1
#else
#define MINIZ_X86_OR_X64_CPU 0
#endif

#if !defined(MINIZ_LITTLE_ENDIAN)
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define MINIZ_LITTLE_ENDIAN 1
#else
#define MINIZ_LITTLE_ENDIAN 0
#endif
#else
#if MINIZ_X86_OR_X64_CPU
#define MINIZ_LITTLE_ENDIAN 1
#else
#define MINIZ_LITTLE_ENDIAN 0
#endif
#endif
#endif

#if defined(__has_feature)
#if __has_feature(undefined_behavior_sanitizer)
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#endif
#endif

#if !defined(MINIZ_USE_UNALIGNED_LOADS_AND_STORES)
#if MINIZ_X86_OR_X64_CPU
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_UNALIGNED_USE_MEMCPY
#else
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#endif
#endif


#ifndef MINIZ_HAS_64BIT_REGISTERS
#define MINIZ_HAS_64BIT_REGISTERS 0
#endif


#define MZ_DEFLATED 8
#define MZ_ADLER32_INIT (1)




#ifndef MINIZ_NO_DEFLATE_APIS


#ifndef TDEFL_LESS_MEMORY
#define TDEFL_LESS_MEMORY 0
#endif

enum {
    TDEFL_HUFFMAN_ONLY = 0,
    TDEFL_DEFAULT_MAX_PROBES = 128,
    TDEFL_MAX_PROBES_MASK = 0xFFF
};

enum {
    TDEFL_WRITE_ZLIB_HEADER             = 0x01000,
    TDEFL_COMPUTE_ADLER32               = 0x02000,
    TDEFL_GREEDY_PARSING_FLAG           = 0x04000,
    TDEFL_NONDETERMINISTIC_PARSING_FLAG = 0x08000,
    TDEFL_RLE_MATCHES                   = 0x10000,
    TDEFL_FILTER_MATCHES                = 0x20000,
    TDEFL_FORCE_ALL_STATIC_BLOCKS       = 0x40000,
    TDEFL_FORCE_ALL_RAW_BLOCKS          = 0x80000
};

typedef mz_bool (*tdefl_put_buf_func_ptr)(const void *pBuf, int len, void *pUser);

enum {
    TDEFL_MAX_HUFF_TABLES    = 3,
    TDEFL_MAX_HUFF_SYMBOLS_0 = 288,
    TDEFL_MAX_HUFF_SYMBOLS_1 = 32,
    TDEFL_MAX_HUFF_SYMBOLS_2 = 19,
    TDEFL_LZ_DICT_SIZE       = 32768,
    TDEFL_LZ_DICT_SIZE_MASK  = TDEFL_LZ_DICT_SIZE - 1,
    TDEFL_MIN_MATCH_LEN      = 3,
    TDEFL_MAX_MATCH_LEN      = 258
};

#if TDEFL_LESS_MEMORY
enum {
    TDEFL_LZ_CODE_BUF_SIZE      = 24 * 1024,
    TDEFL_OUT_BUF_SIZE          = (TDEFL_LZ_CODE_BUF_SIZE * 13) / 10,
    TDEFL_MAX_HUFF_SYMBOLS      = 288,
    TDEFL_LZ_HASH_BITS          = 12,
    TDEFL_LEVEL1_HASH_SIZE_MASK = 4095,
    TDEFL_LZ_HASH_SHIFT         = (TDEFL_LZ_HASH_BITS + 2) / 3,
    TDEFL_LZ_HASH_SIZE          = 1 << TDEFL_LZ_HASH_BITS
};
#else
enum {
    TDEFL_LZ_CODE_BUF_SIZE      = 64 * 1024,
    TDEFL_OUT_BUF_SIZE          = (mz_uint)((TDEFL_LZ_CODE_BUF_SIZE * 13) / 10),
    TDEFL_MAX_HUFF_SYMBOLS      = 288,
    TDEFL_LZ_HASH_BITS          = 15,
    TDEFL_LEVEL1_HASH_SIZE_MASK = 4095,
    TDEFL_LZ_HASH_SHIFT         = (TDEFL_LZ_HASH_BITS + 2) / 3,
    TDEFL_LZ_HASH_SIZE          = 1 << TDEFL_LZ_HASH_BITS
};
#endif

typedef enum {
    TDEFL_STATUS_BAD_PARAM      = -2,
    TDEFL_STATUS_PUT_BUF_FAILED = -1,
    TDEFL_STATUS_OKAY           = 0,
    TDEFL_STATUS_DONE           = 1
} tdefl_status;

typedef enum {
    TDEFL_NO_FLUSH   = 0,
    TDEFL_SYNC_FLUSH = 2,
    TDEFL_FULL_FLUSH = 3,
    TDEFL_FINISH     = 4
} tdefl_flush;

typedef struct {
    tdefl_put_buf_func_ptr m_pPut_buf_func;
    void *m_pPut_buf_user;
    mz_uint m_flags, m_max_probes[2];
    int m_greedy_parsing;
    mz_uint m_adler32, m_lookahead_pos, m_lookahead_size, m_dict_size;
    mz_uint8 *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf, *m_pOutput_buf_end;
    mz_uint m_num_flags_left, m_total_lz_bytes, m_lz_code_buf_dict_pos, m_bits_in, m_bit_buffer;
    mz_uint m_saved_match_dist, m_saved_match_len, m_saved_lit, m_output_flush_ofs, m_output_flush_remaining, m_finished, m_block_index, m_wants_to_finish;
    tdefl_status m_prev_return_status;
    const void *m_pIn_buf;
    void *m_pOut_buf;
    size_t *m_pIn_buf_size, *m_pOut_buf_size;
    tdefl_flush m_flush;
    const mz_uint8 *m_pSrc;
    size_t m_src_buf_left, m_out_buf_ofs;
    mz_uint8 m_dict[TDEFL_LZ_DICT_SIZE + TDEFL_MAX_MATCH_LEN - 1];
    mz_uint16 m_huff_count[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS];
    mz_uint16 m_huff_codes[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS];
    mz_uint8  m_huff_code_sizes[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS];
    mz_uint8  m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE];
    mz_uint16 m_next[TDEFL_LZ_DICT_SIZE];
    mz_uint16 m_hash[TDEFL_LZ_HASH_SIZE];
    mz_uint8  m_output_buf[TDEFL_OUT_BUF_SIZE];
} tdefl_compressor;

MINIZ_EXPORT tdefl_status tdefl_init(tdefl_compressor *d, tdefl_put_buf_func_ptr pPut_buf_func, void *pPut_buf_user, int flags);
MINIZ_EXPORT tdefl_status tdefl_compress(tdefl_compressor *d, const void *pIn_buf, size_t *pIn_buf_size, void *pOut_buf, size_t *pOut_buf_size, tdefl_flush flush);
MINIZ_EXPORT tdefl_status tdefl_compress_buffer(tdefl_compressor *d, const void *pIn_buf, size_t in_buf_size, tdefl_flush flush);
MINIZ_EXPORT mz_uint32    tdefl_get_adler32(tdefl_compressor *d);
MINIZ_EXPORT mz_uint      tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy);

#endif 




#ifndef MINIZ_NO_INFLATE_APIS

enum {
    TINFL_FLAG_PARSE_ZLIB_HEADER            = 1,
    TINFL_FLAG_HAS_MORE_INPUT               = 2,
    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
    TINFL_FLAG_COMPUTE_ADLER32              = 8
};

struct tinfl_decompressor_tag;
typedef struct tinfl_decompressor_tag tinfl_decompressor;

#define TINFL_LZ_DICT_SIZE 32768

typedef enum {
    TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS = -4,
    TINFL_STATUS_BAD_PARAM                   = -3,
    TINFL_STATUS_ADLER32_MISMATCH            = -2,
    TINFL_STATUS_FAILED                      = -1,
    TINFL_STATUS_DONE                        = 0,
    TINFL_STATUS_NEEDS_MORE_INPUT            = 1,
    TINFL_STATUS_HAS_MORE_OUTPUT             = 2
} tinfl_status;

#define tinfl_init(r)     \
    do                    \
    {                     \
        (r)->m_state = 0; \
    }                     \
    MZ_MACRO_END
#define tinfl_get_adler32(r) (r)->m_check_adler32

MINIZ_EXPORT tinfl_status tinfl_decompress(tinfl_decompressor *r, const mz_uint8 *pIn_buf_next, size_t *pIn_buf_size, mz_uint8 *pOut_buf_start, mz_uint8 *pOut_buf_next, size_t *pOut_buf_size, const mz_uint32 decomp_flags);

enum {
    TINFL_MAX_HUFF_TABLES     = 3,
    TINFL_MAX_HUFF_SYMBOLS_0  = 288,
    TINFL_MAX_HUFF_SYMBOLS_1  = 32,
    TINFL_MAX_HUFF_SYMBOLS_2  = 19,
    TINFL_FAST_LOOKUP_BITS    = 10,
    TINFL_FAST_LOOKUP_SIZE    = 1 << TINFL_FAST_LOOKUP_BITS
};


#if MINIZ_HAS_64BIT_REGISTERS
#define TINFL_USE_64BIT_BITBUF 1
#else
#define TINFL_USE_64BIT_BITBUF 0
#endif

#if TINFL_USE_64BIT_BITBUF
typedef mz_uint64 tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (64)
#else
typedef mz_uint32 tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (32)
#endif

struct tinfl_decompressor_tag {
    mz_uint32 m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type,
              m_check_adler32, m_dist, m_counter, m_num_extra,
              m_table_sizes[TINFL_MAX_HUFF_TABLES];
    tinfl_bit_buf_t m_bit_buf;
    size_t m_dist_from_out_buf_start;
    mz_int16 m_look_up[TINFL_MAX_HUFF_TABLES][TINFL_FAST_LOOKUP_SIZE];
    mz_int16 m_tree_0[TINFL_MAX_HUFF_SYMBOLS_0 * 2];
    mz_int16 m_tree_1[TINFL_MAX_HUFF_SYMBOLS_1 * 2];
    mz_int16 m_tree_2[TINFL_MAX_HUFF_SYMBOLS_2 * 2];
    mz_uint8 m_code_size_0[TINFL_MAX_HUFF_SYMBOLS_0];
    mz_uint8 m_code_size_1[TINFL_MAX_HUFF_SYMBOLS_1];
    mz_uint8 m_code_size_2[TINFL_MAX_HUFF_SYMBOLS_2];
    mz_uint8 m_raw_header[4];
    mz_uint8 m_len_codes[TINFL_MAX_HUFF_SYMBOLS_0 + TINFL_MAX_HUFF_SYMBOLS_1 + 137];
};

#endif 



#ifndef MINIZ_NO_ZLIB_APIS

enum {
    MZ_NO_FLUSH      = 0,
    MZ_PARTIAL_FLUSH = 1,
    MZ_SYNC_FLUSH    = 2,
    MZ_FULL_FLUSH    = 3,
    MZ_FINISH        = 4,
    MZ_BLOCK         = 5
};

enum {
    MZ_OK            =  0,
    MZ_STREAM_END    =  1,
    MZ_NEED_DICT     =  2,
    MZ_ERRNO         = -1,
    MZ_STREAM_ERROR  = -2,
    MZ_DATA_ERROR    = -3,
    MZ_MEM_ERROR     = -4,
    MZ_BUF_ERROR     = -5,
    MZ_VERSION_ERROR = -6,
    MZ_PARAM_ERROR   = -10000
};

enum {
    MZ_NO_COMPRESSION   = 0,
    MZ_BEST_SPEED       = 1,
    MZ_BEST_COMPRESSION = 9,
    MZ_UBER_COMPRESSION = 10,
    MZ_DEFAULT_LEVEL    = 6,
    MZ_DEFAULT_COMPRESSION = -1
};

enum {
    MZ_DEFAULT_STRATEGY = 0,
    MZ_FILTERED         = 1,
    MZ_HUFFMAN_ONLY     = 2,
    MZ_RLE              = 3,
    MZ_FIXED            = 4
};

#define MZ_DEFAULT_WINDOW_BITS 15

typedef void *(*mz_alloc_func)(void *opaque, size_t items, size_t size);
typedef void  (*mz_free_func)(void *opaque, void *address);
typedef void *(*mz_realloc_func)(void *opaque, void *address, size_t items, size_t size);

struct mz_internal_state;

typedef struct mz_stream_s {
    const unsigned char *next_in;
    unsigned int avail_in;
    mz_ulong total_in;
    unsigned char *next_out;
    unsigned int avail_out;
    mz_ulong total_out;
    char *msg;
    struct mz_internal_state *state;
    mz_alloc_func zalloc;
    mz_free_func  zfree;
    void *opaque;
    int data_type;
    mz_ulong adler;
    mz_ulong reserved;
} mz_stream;
typedef mz_stream *mz_streamp;

#ifndef MINIZ_NO_DEFLATE_APIS
MINIZ_EXPORT int      mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy);
MINIZ_EXPORT int      mz_deflate(mz_streamp pStream, int flush);
MINIZ_EXPORT int      mz_deflateEnd(mz_streamp pStream);
MINIZ_EXPORT mz_ulong mz_deflateBound(mz_streamp pStream, mz_ulong source_len);
#endif 

#ifndef MINIZ_NO_INFLATE_APIS
MINIZ_EXPORT int mz_inflateInit2(mz_streamp pStream, int window_bits);
MINIZ_EXPORT int mz_inflate(mz_streamp pStream, int flush);
MINIZ_EXPORT int mz_inflateEnd(mz_streamp pStream);
#endif 

MINIZ_EXPORT mz_ulong      mz_adler32(mz_ulong adler, const unsigned char *ptr, size_t buf_len);

#endif 

#ifndef MINIZ_NO_INFLATE_APIS

#ifdef __cplusplus
extern "C"
{
#endif

    

#define TINFL_MEMCPY(d, s, l) memcpy(d, s, l)
#define TINFL_MEMSET(p, c, l) memset(p, c, l)

#define TINFL_CR_BEGIN  \
    switch (r->m_state) \
    {                   \
        case 0:
#define TINFL_CR_RETURN(state_index, result) \
    do                                       \
    {                                        \
        status = result;                     \
        r->m_state = state_index;            \
        goto common_exit;                    \
        case state_index:;                   \
    }                                        \
    MZ_MACRO_END
#define TINFL_CR_RETURN_FOREVER(state_index, result) \
    do                                               \
    {                                                \
        for (;;)                                     \
        {                                            \
            TINFL_CR_RETURN(state_index, result);    \
        }                                            \
    }                                                \
    MZ_MACRO_END
#define TINFL_CR_FINISH }

#define TINFL_GET_BYTE(state_index, c)                                                                                                                           \
    do                                                                                                                                                           \
    {                                                                                                                                                            \
        while (pIn_buf_cur >= pIn_buf_end)                                                                                                                       \
        {                                                                                                                                                        \
            TINFL_CR_RETURN(state_index, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS); \
        }                                                                                                                                                        \
        c = *pIn_buf_cur++;                                                                                                                                      \
    }                                                                                                                                                            \
    MZ_MACRO_END

#define TINFL_NEED_BITS(state_index, n)                \
    do                                                 \
    {                                                  \
        mz_uint c;                                     \
        TINFL_GET_BYTE(state_index, c);                \
        bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); \
        num_bits += 8;                                 \
    } while (num_bits < (mz_uint)(n))
#define TINFL_SKIP_BITS(state_index, n)      \
    do                                       \
    {                                        \
        if (num_bits < (mz_uint)(n))         \
        {                                    \
            TINFL_NEED_BITS(state_index, n); \
        }                                    \
        bit_buf >>= (n);                     \
        num_bits -= (n);                     \
    }                                        \
    MZ_MACRO_END
#define TINFL_GET_BITS(state_index, b, n)    \
    do                                       \
    {                                        \
        if (num_bits < (mz_uint)(n))         \
        {                                    \
            TINFL_NEED_BITS(state_index, n); \
        }                                    \
        b = bit_buf & ((1 << (n)) - 1);      \
        bit_buf >>= (n);                     \
        num_bits -= (n);                     \
    }                                        \
    MZ_MACRO_END





#define TINFL_HUFF_BITBUF_FILL(state_index, pLookUp, pTree)          \
    do                                                               \
    {                                                                \
        temp = pLookUp[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)];      \
        if (temp >= 0)                                               \
        {                                                            \
            code_len = temp >> 9;                                    \
            if ((code_len) && (num_bits >= code_len))                \
                break;                                               \
        }                                                            \
        else if (num_bits > TINFL_FAST_LOOKUP_BITS)                  \
        {                                                            \
            code_len = TINFL_FAST_LOOKUP_BITS;                       \
            do                                                       \
            {                                                        \
                temp = pTree[~temp + ((bit_buf >> code_len++) & 1)]; \
            } while ((temp < 0) && (num_bits >= (code_len + 1)));    \
            if (temp >= 0)                                           \
                break;                                               \
        }                                                            \
        TINFL_GET_BYTE(state_index, c);                              \
        bit_buf |= (((tinfl_bit_buf_t)c) << num_bits);               \
        num_bits += 8;                                               \
    } while (num_bits < 15);







#define TINFL_HUFF_DECODE(state_index, sym, pLookUp, pTree)                                                                         \
    do                                                                                                                              \
    {                                                                                                                               \
        int temp;                                                                                                                   \
        mz_uint code_len, c;                                                                                                        \
        if (num_bits < 15)                                                                                                          \
        {                                                                                                                           \
            if ((pIn_buf_end - pIn_buf_cur) < 2)                                                                                    \
            {                                                                                                                       \
                TINFL_HUFF_BITBUF_FILL(state_index, pLookUp, pTree);                                                                \
            }                                                                                                                       \
            else                                                                                                                    \
            {                                                                                                                       \
                bit_buf |= (((tinfl_bit_buf_t)pIn_buf_cur[0]) << num_bits) | (((tinfl_bit_buf_t)pIn_buf_cur[1]) << (num_bits + 8)); \
                pIn_buf_cur += 2;                                                                                                   \
                num_bits += 16;                                                                                                     \
            }                                                                                                                       \
        }                                                                                                                           \
        if ((temp = pLookUp[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)                                                          \
            code_len = temp >> 9, temp &= 511;                                                                                      \
        else                                                                                                                        \
        {                                                                                                                           \
            code_len = TINFL_FAST_LOOKUP_BITS;                                                                                      \
            do                                                                                                                      \
            {                                                                                                                       \
                temp = pTree[~temp + ((bit_buf >> code_len++) & 1)];                                                                \
            } while (temp < 0);                                                                                                     \
        }                                                                                                                           \
        sym = temp;                                                                                                                 \
        bit_buf >>= code_len;                                                                                                       \
        num_bits -= code_len;                                                                                                       \
    }                                                                                                                               \
    MZ_MACRO_END

    static void tinfl_clear_tree(tinfl_decompressor *r)
    {
        if (r->m_type == 0)
            MZ_CLEAR_ARR(r->m_tree_0);
        else if (r->m_type == 1)
            MZ_CLEAR_ARR(r->m_tree_1);
        else
            MZ_CLEAR_ARR(r->m_tree_2);
    }

    tinfl_status tinfl_decompress(tinfl_decompressor *r, const mz_uint8 *pIn_buf_next, size_t *pIn_buf_size, mz_uint8 *pOut_buf_start, mz_uint8 *pOut_buf_next, size_t *pOut_buf_size, const mz_uint32 decomp_flags)
    {
        static const mz_uint16 s_length_base[31] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0 };
        static const mz_uint8 s_length_extra[31] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0 };
        static const mz_uint16 s_dist_base[32] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0 };
        static const mz_uint8 s_dist_extra[32] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
        static const mz_uint8 s_length_dezigzag[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
        static const mz_uint16 s_min_table_sizes[3] = { 257, 1, 4 };

        mz_int16 *pTrees[3];
        mz_uint8 *pCode_sizes[3];

        tinfl_status status = TINFL_STATUS_FAILED;
        mz_uint32 num_bits, dist, counter, num_extra;
        tinfl_bit_buf_t bit_buf;
        const mz_uint8 *pIn_buf_cur = pIn_buf_next, *const pIn_buf_end = pIn_buf_next + *pIn_buf_size;
        mz_uint8 *pOut_buf_cur = pOut_buf_next, *const pOut_buf_end = pOut_buf_next ? pOut_buf_next + *pOut_buf_size : NULL;
        size_t out_buf_size_mask = (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ? (size_t)-1 : ((pOut_buf_next - pOut_buf_start) + *pOut_buf_size) - 1, dist_from_out_buf_start;

        if (((out_buf_size_mask + 1) & out_buf_size_mask) || (pOut_buf_next < pOut_buf_start))
        {
            *pIn_buf_size = *pOut_buf_size = 0;
            return TINFL_STATUS_BAD_PARAM;
        }

        pTrees[0] = r->m_tree_0;
        pTrees[1] = r->m_tree_1;
        pTrees[2] = r->m_tree_2;
        pCode_sizes[0] = r->m_code_size_0;
        pCode_sizes[1] = r->m_code_size_1;
        pCode_sizes[2] = r->m_code_size_2;

        num_bits = r->m_num_bits;
        bit_buf = r->m_bit_buf;
        dist = r->m_dist;
        counter = r->m_counter;
        num_extra = r->m_num_extra;
        dist_from_out_buf_start = r->m_dist_from_out_buf_start;
        TINFL_CR_BEGIN

        bit_buf = num_bits = dist = counter = num_extra = r->m_zhdr0 = r->m_zhdr1 = 0;
        r->m_z_adler32 = r->m_check_adler32 = 1;
        if (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER)
        {
            TINFL_GET_BYTE(1, r->m_zhdr0);
            TINFL_GET_BYTE(2, r->m_zhdr1);
            counter = (((r->m_zhdr0 * 256 + r->m_zhdr1) % 31 != 0) || (r->m_zhdr1 & 32) || ((r->m_zhdr0 & 15) != 8));
            if (!(decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
                counter |= (((1U << (8U + (r->m_zhdr0 >> 4))) > 32768U) || ((out_buf_size_mask + 1) < (size_t)((size_t)1 << (8U + (r->m_zhdr0 >> 4)))));
            if (counter)
            {
                TINFL_CR_RETURN_FOREVER(36, TINFL_STATUS_FAILED);
            }
        }

        do
        {
            TINFL_GET_BITS(3, r->m_final, 3);
            r->m_type = r->m_final >> 1;
            if (r->m_type == 0)
            {
                TINFL_SKIP_BITS(5, num_bits & 7);
                for (counter = 0; counter < 4; ++counter)
                {
                    if (num_bits)
                        TINFL_GET_BITS(6, r->m_raw_header[counter], 8);
                    else
                        TINFL_GET_BYTE(7, r->m_raw_header[counter]);
                }
                if ((counter = (r->m_raw_header[0] | (r->m_raw_header[1] << 8))) != (mz_uint)(0xFFFF ^ (r->m_raw_header[2] | (r->m_raw_header[3] << 8))))
                {
                    TINFL_CR_RETURN_FOREVER(39, TINFL_STATUS_FAILED);
                }
                while ((counter) && (num_bits))
                {
                    TINFL_GET_BITS(51, dist, 8);
                    while (pOut_buf_cur >= pOut_buf_end)
                    {
                        TINFL_CR_RETURN(52, TINFL_STATUS_HAS_MORE_OUTPUT);
                    }
                    *pOut_buf_cur++ = (mz_uint8)dist;
                    counter--;
                }
                while (counter)
                {
                    size_t n;
                    while (pOut_buf_cur >= pOut_buf_end)
                    {
                        TINFL_CR_RETURN(9, TINFL_STATUS_HAS_MORE_OUTPUT);
                    }
                    while (pIn_buf_cur >= pIn_buf_end)
                    {
                        TINFL_CR_RETURN(38, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS);
                    }
                    n = MZ_MIN(MZ_MIN((size_t)(pOut_buf_end - pOut_buf_cur), (size_t)(pIn_buf_end - pIn_buf_cur)), counter);
                    TINFL_MEMCPY(pOut_buf_cur, pIn_buf_cur, n);
                    pIn_buf_cur += n;
                    pOut_buf_cur += n;
                    counter -= (mz_uint)n;
                }
            }
            else if (r->m_type == 3)
            {
                TINFL_CR_RETURN_FOREVER(10, TINFL_STATUS_FAILED);
            }
            else
            {
                if (r->m_type == 1)
                {
                    mz_uint8 *p = r->m_code_size_0;
                    mz_uint i;
                    r->m_table_sizes[0] = 288;
                    r->m_table_sizes[1] = 32;
                    TINFL_MEMSET(r->m_code_size_1, 5, 32);
                    for (i = 0; i <= 143; ++i)
                        *p++ = 8;
                    for (; i <= 255; ++i)
                        *p++ = 9;
                    for (; i <= 279; ++i)
                        *p++ = 7;
                    for (; i <= 287; ++i)
                        *p++ = 8;
                }
                else
                {
                    for (counter = 0; counter < 3; counter++)
                    {
                        TINFL_GET_BITS(11, r->m_table_sizes[counter], "\05\05\04"[counter]);
                        r->m_table_sizes[counter] += s_min_table_sizes[counter];
                    }
                    MZ_CLEAR_ARR(r->m_code_size_2);
                    for (counter = 0; counter < r->m_table_sizes[2]; counter++)
                    {
                        mz_uint s;
                        TINFL_GET_BITS(14, s, 3);
                        r->m_code_size_2[s_length_dezigzag[counter]] = (mz_uint8)s;
                    }
                    r->m_table_sizes[2] = 19;
                }
                for (; (int)r->m_type >= 0; r->m_type--)
                {
                    int tree_next, tree_cur;
                    mz_int16 *pLookUp;
                    mz_int16 *pTree;
                    mz_uint8 *pCode_size;
                    mz_uint i, j, used_syms, total, sym_index, next_code[17], total_syms[16];
                    pLookUp = r->m_look_up[r->m_type];
                    pTree = pTrees[r->m_type];
                    pCode_size = pCode_sizes[r->m_type];
                    MZ_CLEAR_ARR(total_syms);
                    TINFL_MEMSET(pLookUp, 0, sizeof(r->m_look_up[0]));
                    tinfl_clear_tree(r);
                    for (i = 0; i < r->m_table_sizes[r->m_type]; ++i)
                        total_syms[pCode_size[i]]++;
                    used_syms = 0, total = 0;
                    next_code[0] = next_code[1] = 0;
                    for (i = 1; i <= 15; ++i)
                    {
                        used_syms += total_syms[i];
                        next_code[i + 1] = (total = ((total + total_syms[i]) << 1));
                    }
                    if ((65536 != total) && (used_syms > 1))
                    {
                        TINFL_CR_RETURN_FOREVER(35, TINFL_STATUS_FAILED);
                    }
                    for (tree_next = -1, sym_index = 0; sym_index < r->m_table_sizes[r->m_type]; ++sym_index)
                    {
                        mz_uint rev_code = 0, l, cur_code, code_size = pCode_size[sym_index];
                        if (!code_size)
                            continue;
                        cur_code = next_code[code_size]++;
                        for (l = code_size; l > 0; l--, cur_code >>= 1)
                            rev_code = (rev_code << 1) | (cur_code & 1);
                        if (code_size <= TINFL_FAST_LOOKUP_BITS)
                        {
                            mz_int16 k = (mz_int16)((code_size << 9) | sym_index);
                            while (rev_code < TINFL_FAST_LOOKUP_SIZE)
                            {
                                pLookUp[rev_code] = k;
                                rev_code += (1 << code_size);
                            }
                            continue;
                        }
                        if (0 == (tree_cur = pLookUp[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)]))
                        {
                            pLookUp[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)] = (mz_int16)tree_next;
                            tree_cur = tree_next;
                            tree_next -= 2;
                        }
                        rev_code >>= (TINFL_FAST_LOOKUP_BITS - 1);
                        for (j = code_size; j > (TINFL_FAST_LOOKUP_BITS + 1); j--)
                        {
                            tree_cur -= ((rev_code >>= 1) & 1);
                            if (!pTree[-tree_cur - 1])
                            {
                                pTree[-tree_cur - 1] = (mz_int16)tree_next;
                                tree_cur = tree_next;
                                tree_next -= 2;
                            }
                            else
                                tree_cur = pTree[-tree_cur - 1];
                        }
                        tree_cur -= ((rev_code >>= 1) & 1);
                        pTree[-tree_cur - 1] = (mz_int16)sym_index;
                    }
                    if (r->m_type == 2)
                    {
                        for (counter = 0; counter < (r->m_table_sizes[0] + r->m_table_sizes[1]);)
                        {
                            mz_uint s;
                            TINFL_HUFF_DECODE(16, dist, r->m_look_up[2], r->m_tree_2);
                            if (dist < 16)
                            {
                                r->m_len_codes[counter++] = (mz_uint8)dist;
                                continue;
                            }
                            if ((dist == 16) && (!counter))
                            {
                                TINFL_CR_RETURN_FOREVER(17, TINFL_STATUS_FAILED);
                            }
                            num_extra = "\02\03\07"[dist - 16];
                            TINFL_GET_BITS(18, s, num_extra);
                            s += "\03\03\013"[dist - 16];
                            TINFL_MEMSET(r->m_len_codes + counter, (dist == 16) ? r->m_len_codes[counter - 1] : 0, s);
                            counter += s;
                        }
                        if ((r->m_table_sizes[0] + r->m_table_sizes[1]) != counter)
                        {
                            TINFL_CR_RETURN_FOREVER(21, TINFL_STATUS_FAILED);
                        }
                        TINFL_MEMCPY(r->m_code_size_0, r->m_len_codes, r->m_table_sizes[0]);
                        TINFL_MEMCPY(r->m_code_size_1, r->m_len_codes + r->m_table_sizes[0], r->m_table_sizes[1]);
                    }
                }
                for (;;)
                {
                    mz_uint8 *pSrc;
                    for (;;)
                    {
                        if (((pIn_buf_end - pIn_buf_cur) < 4) || ((pOut_buf_end - pOut_buf_cur) < 2))
                        {
                            TINFL_HUFF_DECODE(23, counter, r->m_look_up[0], r->m_tree_0);
                            if (counter >= 256)
                                break;
                            while (pOut_buf_cur >= pOut_buf_end)
                            {
                                TINFL_CR_RETURN(24, TINFL_STATUS_HAS_MORE_OUTPUT);
                            }
                            *pOut_buf_cur++ = (mz_uint8)counter;
                        }
                        else
                        {
                            int sym2;
                            mz_uint code_len;
#if TINFL_USE_64BIT_BITBUF
                            if (num_bits < 30)
                            {
                                bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE32(pIn_buf_cur)) << num_bits);
                                pIn_buf_cur += 4;
                                num_bits += 32;
                            }
#else
                        if (num_bits < 15)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 2;
                            num_bits += 16;
                        }
#endif
                            if ((sym2 = r->m_look_up[0][bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
                                code_len = sym2 >> 9;
                            else
                            {
                                code_len = TINFL_FAST_LOOKUP_BITS;
                                do
                                {
                                    sym2 = r->m_tree_0[~sym2 + ((bit_buf >> code_len++) & 1)];
                                } while (sym2 < 0);
                            }
                            counter = sym2;
                            bit_buf >>= code_len;
                            num_bits -= code_len;
                            if (code_len == 0)
                            {
                                TINFL_CR_RETURN_FOREVER(40, TINFL_STATUS_FAILED);
                            }
                            if (counter & 256)
                                break;

#if !TINFL_USE_64BIT_BITBUF
                            if (num_bits < 15)
                            {
                                bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits);
                                pIn_buf_cur += 2;
                                num_bits += 16;
                            }
#endif
                            if ((sym2 = r->m_look_up[0][bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
                                code_len = sym2 >> 9;
                            else
                            {
                                code_len = TINFL_FAST_LOOKUP_BITS;
                                do
                                {
                                    sym2 = r->m_tree_0[~sym2 + ((bit_buf >> code_len++) & 1)];
                                } while (sym2 < 0);
                            }
                            bit_buf >>= code_len;
                            num_bits -= code_len;
                            if (code_len == 0)
                            {
                                TINFL_CR_RETURN_FOREVER(54, TINFL_STATUS_FAILED);
                            }

                            pOut_buf_cur[0] = (mz_uint8)counter;
                            if (sym2 & 256)
                            {
                                pOut_buf_cur++;
                                counter = sym2;
                                break;
                            }
                            pOut_buf_cur[1] = (mz_uint8)sym2;
                            pOut_buf_cur += 2;
                        }
                    }
                    if ((counter &= 511) == 256)
                        break;

                    num_extra = s_length_extra[counter - 257];
                    counter = s_length_base[counter - 257];
                    if (num_extra)
                    {
                        mz_uint extra_bits;
                        TINFL_GET_BITS(25, extra_bits, num_extra);
                        counter += extra_bits;
                    }

                    TINFL_HUFF_DECODE(26, dist, r->m_look_up[1], r->m_tree_1);
                    num_extra = s_dist_extra[dist];
                    dist = s_dist_base[dist];
                    if (num_extra)
                    {
                        mz_uint extra_bits;
                        TINFL_GET_BITS(27, extra_bits, num_extra);
                        dist += extra_bits;
                    }

                    dist_from_out_buf_start = pOut_buf_cur - pOut_buf_start;
                    if ((dist == 0 || dist > dist_from_out_buf_start || dist_from_out_buf_start == 0) && (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
                    {
                        TINFL_CR_RETURN_FOREVER(37, TINFL_STATUS_FAILED);
                    }

                    pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);

                    if ((MZ_MAX(pOut_buf_cur, pSrc) + counter) > pOut_buf_end)
                    {
                        while (counter--)
                        {
                            while (pOut_buf_cur >= pOut_buf_end)
                            {
                                TINFL_CR_RETURN(53, TINFL_STATUS_HAS_MORE_OUTPUT);
                            }
                            *pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
                        }
                        continue;
                    }
#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES
                    else if ((counter >= 9) && (counter <= dist))
                    {
                        const mz_uint8 *pSrc_end = pSrc + (counter & ~7);
                        do
                        {
#ifdef MINIZ_UNALIGNED_USE_MEMCPY
                            memcpy(pOut_buf_cur, pSrc, sizeof(mz_uint32) * 2);
#else
                            ((mz_uint32 *)pOut_buf_cur)[0] = ((const mz_uint32 *)pSrc)[0];
                            ((mz_uint32 *)pOut_buf_cur)[1] = ((const mz_uint32 *)pSrc)[1];
#endif
                            pOut_buf_cur += 8;
                        } while ((pSrc += 8) < pSrc_end);
                        if ((counter &= 7) < 3)
                        {
                            if (counter)
                            {
                                pOut_buf_cur[0] = pSrc[0];
                                if (counter > 1)
                                    pOut_buf_cur[1] = pSrc[1];
                                pOut_buf_cur += counter;
                            }
                            continue;
                        }
                    }
#endif
                    while (counter > 2)
                    {
                        pOut_buf_cur[0] = pSrc[0];
                        pOut_buf_cur[1] = pSrc[1];
                        pOut_buf_cur[2] = pSrc[2];
                        pOut_buf_cur += 3;
                        pSrc += 3;
                        counter -= 3;
                    }
                    if (counter > 0)
                    {
                        pOut_buf_cur[0] = pSrc[0];
                        if (counter > 1)
                            pOut_buf_cur[1] = pSrc[1];
                        pOut_buf_cur += counter;
                    }
                }
            }
        } while (!(r->m_final & 1));

        TINFL_SKIP_BITS(32, num_bits & 7);
        while ((pIn_buf_cur > pIn_buf_next) && (num_bits >= 8))
        {
            --pIn_buf_cur;
            num_bits -= 8;
        }
        bit_buf &= ~(~(tinfl_bit_buf_t)0 << num_bits);
        MZ_ASSERT(!num_bits);

        if (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER)
        {
            for (counter = 0; counter < 4; ++counter)
            {
                mz_uint s;
                if (num_bits)
                    TINFL_GET_BITS(41, s, 8);
                else
                    TINFL_GET_BYTE(42, s);
                r->m_z_adler32 = (r->m_z_adler32 << 8) | s;
            }
        }
        TINFL_CR_RETURN_FOREVER(34, TINFL_STATUS_DONE);

        TINFL_CR_FINISH

    common_exit:
        if ((status != TINFL_STATUS_NEEDS_MORE_INPUT) && (status != TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS))
        {
            while ((pIn_buf_cur > pIn_buf_next) && (num_bits >= 8))
            {
                --pIn_buf_cur;
                num_bits -= 8;
            }
        }
        r->m_num_bits = num_bits;
        r->m_bit_buf = bit_buf & ~(~(tinfl_bit_buf_t)0 << num_bits);
        r->m_dist = dist;
        r->m_counter = counter;
        r->m_num_extra = num_extra;
        r->m_dist_from_out_buf_start = dist_from_out_buf_start;
        *pIn_buf_size = pIn_buf_cur - pIn_buf_next;
        *pOut_buf_size = pOut_buf_cur - pOut_buf_next;
        if ((decomp_flags & (TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32)) && (status >= 0))
        {
            const mz_uint8 *ptr = pOut_buf_next;
            size_t buf_len = *pOut_buf_size;
            mz_uint32 i, s1 = r->m_check_adler32 & 0xffff, s2 = r->m_check_adler32 >> 16;
            size_t block_len = buf_len % 5552;
            while (buf_len)
            {
                for (i = 0; i + 7 < block_len; i += 8, ptr += 8)
                {
                    s1 += ptr[0], s2 += s1;
                    s1 += ptr[1], s2 += s1;
                    s1 += ptr[2], s2 += s1;
                    s1 += ptr[3], s2 += s1;
                    s1 += ptr[4], s2 += s1;
                    s1 += ptr[5], s2 += s1;
                    s1 += ptr[6], s2 += s1;
                    s1 += ptr[7], s2 += s1;
                }
                for (; i < block_len; ++i)
                    s1 += *ptr++, s2 += s1;
                s1 %= 65521U, s2 %= 65521U;
                buf_len -= block_len;
                block_len = 5552;
            }
            r->m_check_adler32 = (s2 << 16) + s1;
            if ((status == TINFL_STATUS_DONE) && (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER) && (r->m_check_adler32 != r->m_z_adler32))
                status = TINFL_STATUS_ADLER32_MISMATCH;
        }
        return status;
    }

#ifdef __cplusplus
}
#endif

#endif 







#ifndef MINIZ_NO_DEFLATE_APIS

#ifdef __cplusplus
extern "C"
{
#endif

    

    
    static const mz_uint16 s_tdefl_len_sym[256] = {
        257, 258, 259, 260, 261, 262, 263, 264, 265, 265, 266, 266, 267, 267, 268, 268, 269, 269, 269, 269, 270, 270, 270, 270, 271, 271, 271, 271, 272, 272, 272, 272,
        273, 273, 273, 273, 273, 273, 273, 273, 274, 274, 274, 274, 274, 274, 274, 274, 275, 275, 275, 275, 275, 275, 275, 275, 276, 276, 276, 276, 276, 276, 276, 276,
        277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278,
        279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280,
        281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281,
        282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282,
        283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283,
        284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 285
    };

    static const mz_uint8 s_tdefl_len_extra[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0
    };

    static const mz_uint8 s_tdefl_small_dist_sym[512] = {
        0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
        11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13,
        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
        14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
        14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
        17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
        17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
        17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17
    };

    static const mz_uint8 s_tdefl_small_dist_extra[512] = {
        0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7
    };

    static const mz_uint8 s_tdefl_large_dist_sym[128] = {
        0, 0, 18, 19, 20, 20, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
        26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
        28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29
    };

    static const mz_uint8 s_tdefl_large_dist_extra[128] = {
        0, 0, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
        12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13
    };

    typedef struct
    {
        mz_uint16 m_key, m_sym_index;
    } tdefl_sym_freq;
    static tdefl_sym_freq *tdefl_radix_sort_syms(mz_uint num_syms, tdefl_sym_freq *pSyms0, tdefl_sym_freq *pSyms1)
    {
        mz_uint32 total_passes = 2, pass_shift, pass, i, hist[256 * 2];
        tdefl_sym_freq *pCur_syms = pSyms0, *pNew_syms = pSyms1;
        MZ_CLEAR_ARR(hist);
        for (i = 0; i < num_syms; i++)
        {
            mz_uint freq = pSyms0[i].m_key;
            hist[freq & 0xFF]++;
            hist[256 + ((freq >> 8) & 0xFF)]++;
        }
        while ((total_passes > 1) && (num_syms == hist[(total_passes - 1) * 256]))
            total_passes--;
        for (pass_shift = 0, pass = 0; pass < total_passes; pass++, pass_shift += 8)
        {
            const mz_uint32 *pHist = &hist[pass << 8];
            mz_uint offsets[256], cur_ofs = 0;
            for (i = 0; i < 256; i++)
            {
                offsets[i] = cur_ofs;
                cur_ofs += pHist[i];
            }
            for (i = 0; i < num_syms; i++)
                pNew_syms[offsets[(pCur_syms[i].m_key >> pass_shift) & 0xFF]++] = pCur_syms[i];
            {
                tdefl_sym_freq *t = pCur_syms;
                pCur_syms = pNew_syms;
                pNew_syms = t;
            }
        }
        return pCur_syms;
    }

    static void tdefl_calculate_minimum_redundancy(tdefl_sym_freq *A, int n)
    {
        int root, leaf, next, avbl, used, dpth;
        if (n == 0)
            return;
        else if (n == 1)
        {
            A[0].m_key = 1;
            return;
        }
        A[0].m_key += A[1].m_key;
        root = 0;
        leaf = 2;
        for (next = 1; next < n - 1; next++)
        {
            if (leaf >= n || A[root].m_key < A[leaf].m_key)
            {
                A[next].m_key = A[root].m_key;
                A[root++].m_key = (mz_uint16)next;
            }
            else
                A[next].m_key = A[leaf++].m_key;
            if (leaf >= n || (root < next && A[root].m_key < A[leaf].m_key))
            {
                A[next].m_key = (mz_uint16)(A[next].m_key + A[root].m_key);
                A[root++].m_key = (mz_uint16)next;
            }
            else
                A[next].m_key = (mz_uint16)(A[next].m_key + A[leaf++].m_key);
        }
        A[n - 2].m_key = 0;
        for (next = n - 3; next >= 0; next--)
            A[next].m_key = A[A[next].m_key].m_key + 1;
        avbl = 1;
        used = dpth = 0;
        root = n - 2;
        next = n - 1;
        while (avbl > 0)
        {
            while (root >= 0 && (int)A[root].m_key == dpth)
            {
                used++;
                root--;
            }
            while (avbl > used)
            {
                A[next--].m_key = (mz_uint16)(dpth);
                avbl--;
            }
            avbl = 2 * used;
            dpth++;
            used = 0;
        }
    }

    enum
    {
        TDEFL_MAX_SUPPORTED_HUFF_CODESIZE = 32
    };
    static void tdefl_huffman_enforce_max_code_size(int *pNum_codes, int code_list_len, int max_code_size)
    {
        int i;
        mz_uint32 total = 0;
        if (code_list_len <= 1)
            return;
        for (i = max_code_size + 1; i <= TDEFL_MAX_SUPPORTED_HUFF_CODESIZE; i++)
            pNum_codes[max_code_size] += pNum_codes[i];
        for (i = max_code_size; i > 0; i--)
            total += (((mz_uint32)pNum_codes[i]) << (max_code_size - i));
        while (total != (1UL << max_code_size))
        {
            pNum_codes[max_code_size]--;
            for (i = max_code_size - 1; i > 0; i--)
                if (pNum_codes[i])
                {
                    pNum_codes[i]--;
                    pNum_codes[i + 1] += 2;
                    break;
                }
            total--;
        }
    }

    static void tdefl_optimize_huffman_table(tdefl_compressor *d, int table_num, int table_len, int code_size_limit, int static_table)
    {
        int i, j, l, num_codes[1 + TDEFL_MAX_SUPPORTED_HUFF_CODESIZE];
        mz_uint next_code[TDEFL_MAX_SUPPORTED_HUFF_CODESIZE + 1];
        MZ_CLEAR_ARR(num_codes);
        if (static_table)
        {
            for (i = 0; i < table_len; i++)
                num_codes[d->m_huff_code_sizes[table_num][i]]++;
        }
        else
        {
            tdefl_sym_freq syms0[TDEFL_MAX_HUFF_SYMBOLS], syms1[TDEFL_MAX_HUFF_SYMBOLS], *pSyms;
            int num_used_syms = 0;
            const mz_uint16 *pSym_count = &d->m_huff_count[table_num][0];
            for (i = 0; i < table_len; i++)
                if (pSym_count[i])
                {
                    syms0[num_used_syms].m_key = (mz_uint16)pSym_count[i];
                    syms0[num_used_syms++].m_sym_index = (mz_uint16)i;
                }

            pSyms = tdefl_radix_sort_syms(num_used_syms, syms0, syms1);
            tdefl_calculate_minimum_redundancy(pSyms, num_used_syms);

            for (i = 0; i < num_used_syms; i++)
                num_codes[pSyms[i].m_key]++;

            tdefl_huffman_enforce_max_code_size(num_codes, num_used_syms, code_size_limit);

            MZ_CLEAR_ARR(d->m_huff_code_sizes[table_num]);
            MZ_CLEAR_ARR(d->m_huff_codes[table_num]);
            for (i = 1, j = num_used_syms; i <= code_size_limit; i++)
                for (l = num_codes[i]; l > 0; l--)
                    d->m_huff_code_sizes[table_num][pSyms[--j].m_sym_index] = (mz_uint8)(i);
        }

        next_code[1] = 0;
        for (j = 0, i = 2; i <= code_size_limit; i++)
            next_code[i] = j = ((j + num_codes[i - 1]) << 1);

        for (i = 0; i < table_len; i++)
        {
            mz_uint rev_code = 0, code, code_size;
            if ((code_size = d->m_huff_code_sizes[table_num][i]) == 0)
                continue;
            code = next_code[code_size]++;
            for (l = code_size; l > 0; l--, code >>= 1)
                rev_code = (rev_code << 1) | (code & 1);
            d->m_huff_codes[table_num][i] = (mz_uint16)rev_code;
        }
    }

#define TDEFL_PUT_BITS(b, l)                                       \
    do                                                             \
    {                                                              \
        mz_uint bits = b;                                          \
        mz_uint len = l;                                           \
        MZ_ASSERT(bits <= ((1U << len) - 1U));                     \
        d->m_bit_buffer |= (bits << d->m_bits_in);                 \
        d->m_bits_in += len;                                       \
        while (d->m_bits_in >= 8)                                  \
        {                                                          \
            if (d->m_pOutput_buf < d->m_pOutput_buf_end)           \
                *d->m_pOutput_buf++ = (mz_uint8)(d->m_bit_buffer); \
            d->m_bit_buffer >>= 8;                                 \
            d->m_bits_in -= 8;                                     \
        }                                                          \
    }                                                              \
    MZ_MACRO_END

#define TDEFL_RLE_PREV_CODE_SIZE()                                                                                       \
    {                                                                                                                    \
        if (rle_repeat_count)                                                                                            \
        {                                                                                                                \
            if (rle_repeat_count < 3)                                                                                    \
            {                                                                                                            \
                d->m_huff_count[2][prev_code_size] = (mz_uint16)(d->m_huff_count[2][prev_code_size] + rle_repeat_count); \
                while (rle_repeat_count--)                                                                               \
                    packed_code_sizes[num_packed_code_sizes++] = prev_code_size;                                         \
            }                                                                                                            \
            else                                                                                                         \
            {                                                                                                            \
                d->m_huff_count[2][16] = (mz_uint16)(d->m_huff_count[2][16] + 1);                                        \
                packed_code_sizes[num_packed_code_sizes++] = 16;                                                         \
                packed_code_sizes[num_packed_code_sizes++] = (mz_uint8)(rle_repeat_count - 3);                           \
            }                                                                                                            \
            rle_repeat_count = 0;                                                                                        \
        }                                                                                                                \
    }

#define TDEFL_RLE_ZERO_CODE_SIZE()                                                         \
    {                                                                                      \
        if (rle_z_count)                                                                   \
        {                                                                                  \
            if (rle_z_count < 3)                                                           \
            {                                                                              \
                d->m_huff_count[2][0] = (mz_uint16)(d->m_huff_count[2][0] + rle_z_count);  \
                while (rle_z_count--)                                                      \
                    packed_code_sizes[num_packed_code_sizes++] = 0;                        \
            }                                                                              \
            else if (rle_z_count <= 10)                                                    \
            {                                                                              \
                d->m_huff_count[2][17] = (mz_uint16)(d->m_huff_count[2][17] + 1);          \
                packed_code_sizes[num_packed_code_sizes++] = 17;                           \
                packed_code_sizes[num_packed_code_sizes++] = (mz_uint8)(rle_z_count - 3);  \
            }                                                                              \
            else                                                                           \
            {                                                                              \
                d->m_huff_count[2][18] = (mz_uint16)(d->m_huff_count[2][18] + 1);          \
                packed_code_sizes[num_packed_code_sizes++] = 18;                           \
                packed_code_sizes[num_packed_code_sizes++] = (mz_uint8)(rle_z_count - 11); \
            }                                                                              \
            rle_z_count = 0;                                                               \
        }                                                                                  \
    }

    static const mz_uint8 s_tdefl_packed_code_size_syms_swizzle[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

    static void tdefl_start_dynamic_block(tdefl_compressor *d)
    {
        int num_lit_codes, num_dist_codes, num_bit_lengths;
        mz_uint i, total_code_sizes_to_pack, num_packed_code_sizes, rle_z_count, rle_repeat_count, packed_code_sizes_index;
        mz_uint8 code_sizes_to_pack[TDEFL_MAX_HUFF_SYMBOLS_0 + TDEFL_MAX_HUFF_SYMBOLS_1], packed_code_sizes[TDEFL_MAX_HUFF_SYMBOLS_0 + TDEFL_MAX_HUFF_SYMBOLS_1], prev_code_size = 0xFF;

        d->m_huff_count[0][256] = 1;

        tdefl_optimize_huffman_table(d, 0, TDEFL_MAX_HUFF_SYMBOLS_0, 15, MZ_FALSE);
        tdefl_optimize_huffman_table(d, 1, TDEFL_MAX_HUFF_SYMBOLS_1, 15, MZ_FALSE);

        for (num_lit_codes = 286; num_lit_codes > 257; num_lit_codes--)
            if (d->m_huff_code_sizes[0][num_lit_codes - 1])
                break;
        for (num_dist_codes = 30; num_dist_codes > 1; num_dist_codes--)
            if (d->m_huff_code_sizes[1][num_dist_codes - 1])
                break;

        memcpy(code_sizes_to_pack, &d->m_huff_code_sizes[0][0], num_lit_codes);
        memcpy(code_sizes_to_pack + num_lit_codes, &d->m_huff_code_sizes[1][0], num_dist_codes);
        total_code_sizes_to_pack = num_lit_codes + num_dist_codes;
        num_packed_code_sizes = 0;
        rle_z_count = 0;
        rle_repeat_count = 0;

        memset(&d->m_huff_count[2][0], 0, sizeof(d->m_huff_count[2][0]) * TDEFL_MAX_HUFF_SYMBOLS_2);
        for (i = 0; i < total_code_sizes_to_pack; i++)
        {
            mz_uint8 code_size = code_sizes_to_pack[i];
            if (!code_size)
            {
                TDEFL_RLE_PREV_CODE_SIZE();
                if (++rle_z_count == 138)
                {
                    TDEFL_RLE_ZERO_CODE_SIZE();
                }
            }
            else
            {
                TDEFL_RLE_ZERO_CODE_SIZE();
                if (code_size != prev_code_size)
                {
                    TDEFL_RLE_PREV_CODE_SIZE();
                    d->m_huff_count[2][code_size] = (mz_uint16)(d->m_huff_count[2][code_size] + 1);
                    packed_code_sizes[num_packed_code_sizes++] = code_size;
                }
                else if (++rle_repeat_count == 6)
                {
                    TDEFL_RLE_PREV_CODE_SIZE();
                }
            }
            prev_code_size = code_size;
        }
        if (rle_repeat_count)
        {
            TDEFL_RLE_PREV_CODE_SIZE();
        }
        else
        {
            TDEFL_RLE_ZERO_CODE_SIZE();
        }

        tdefl_optimize_huffman_table(d, 2, TDEFL_MAX_HUFF_SYMBOLS_2, 7, MZ_FALSE);

        TDEFL_PUT_BITS(2, 2);

        TDEFL_PUT_BITS(num_lit_codes - 257, 5);
        TDEFL_PUT_BITS(num_dist_codes - 1, 5);

        for (num_bit_lengths = 18; num_bit_lengths >= 0; num_bit_lengths--)
            if (d->m_huff_code_sizes[2][s_tdefl_packed_code_size_syms_swizzle[num_bit_lengths]])
                break;
        num_bit_lengths = MZ_MAX(4, (num_bit_lengths + 1));
        TDEFL_PUT_BITS(num_bit_lengths - 4, 4);
        for (i = 0; (int)i < num_bit_lengths; i++)
            TDEFL_PUT_BITS(d->m_huff_code_sizes[2][s_tdefl_packed_code_size_syms_swizzle[i]], 3);

        for (packed_code_sizes_index = 0; packed_code_sizes_index < num_packed_code_sizes;)
        {
            mz_uint code = packed_code_sizes[packed_code_sizes_index++];
            MZ_ASSERT(code < TDEFL_MAX_HUFF_SYMBOLS_2);
            TDEFL_PUT_BITS(d->m_huff_codes[2][code], d->m_huff_code_sizes[2][code]);
            if (code >= 16)
                TDEFL_PUT_BITS(packed_code_sizes[packed_code_sizes_index++], "\02\03\07"[code - 16]);
        }
    }

    static void tdefl_start_static_block(tdefl_compressor *d)
    {
        mz_uint i;
        mz_uint8 *p = &d->m_huff_code_sizes[0][0];

        for (i = 0; i <= 143; ++i)
            *p++ = 8;
        for (; i <= 255; ++i)
            *p++ = 9;
        for (; i <= 279; ++i)
            *p++ = 7;
        for (; i <= 287; ++i)
            *p++ = 8;

        memset(d->m_huff_code_sizes[1], 5, 32);

        tdefl_optimize_huffman_table(d, 0, 288, 15, MZ_TRUE);
        tdefl_optimize_huffman_table(d, 1, 32, 15, MZ_TRUE);

        TDEFL_PUT_BITS(1, 2);
    }

    static const mz_uint mz_bitmasks[17] = { 0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF };

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN && MINIZ_HAS_64BIT_REGISTERS
    static mz_bool tdefl_compress_lz_codes(tdefl_compressor *d)
    {
        mz_uint flags;
        mz_uint8 *pLZ_codes;
        mz_uint8 *pOutput_buf = d->m_pOutput_buf;
        mz_uint8 *pLZ_code_buf_end = d->m_pLZ_code_buf;
        mz_uint64 bit_buffer = d->m_bit_buffer;
        mz_uint bits_in = d->m_bits_in;

#define TDEFL_PUT_BITS_FAST(b, l)                    \
    {                                                \
        bit_buffer |= (((mz_uint64)(b)) << bits_in); \
        bits_in += (l);                              \
    }

        flags = 1;
        for (pLZ_codes = d->m_lz_code_buf; pLZ_codes < pLZ_code_buf_end; flags >>= 1)
        {
            if (flags == 1)
                flags = *pLZ_codes++ | 0x100;

            if (flags & 1)
            {
                mz_uint s0, s1, n0, n1, sym, num_extra_bits;
                mz_uint match_len = pLZ_codes[0];
                mz_uint match_dist = (pLZ_codes[1] | (pLZ_codes[2] << 8));
                pLZ_codes += 3;

                MZ_ASSERT(d->m_huff_code_sizes[0][s_tdefl_len_sym[match_len]]);
                TDEFL_PUT_BITS_FAST(d->m_huff_codes[0][s_tdefl_len_sym[match_len]], d->m_huff_code_sizes[0][s_tdefl_len_sym[match_len]]);
                TDEFL_PUT_BITS_FAST(match_len & mz_bitmasks[s_tdefl_len_extra[match_len]], s_tdefl_len_extra[match_len]);

                s0 = s_tdefl_small_dist_sym[match_dist & 511];
                n0 = s_tdefl_small_dist_extra[match_dist & 511];
                s1 = s_tdefl_large_dist_sym[match_dist >> 8];
                n1 = s_tdefl_large_dist_extra[match_dist >> 8];
                sym = (match_dist < 512) ? s0 : s1;
                num_extra_bits = (match_dist < 512) ? n0 : n1;

                MZ_ASSERT(d->m_huff_code_sizes[1][sym]);
                TDEFL_PUT_BITS_FAST(d->m_huff_codes[1][sym], d->m_huff_code_sizes[1][sym]);
                TDEFL_PUT_BITS_FAST(match_dist & mz_bitmasks[num_extra_bits], num_extra_bits);
            }
            else
            {
                mz_uint lit = *pLZ_codes++;
                MZ_ASSERT(d->m_huff_code_sizes[0][lit]);
                TDEFL_PUT_BITS_FAST(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);

                if (((flags & 2) == 0) && (pLZ_codes < pLZ_code_buf_end))
                {
                    flags >>= 1;
                    lit = *pLZ_codes++;
                    MZ_ASSERT(d->m_huff_code_sizes[0][lit]);
                    TDEFL_PUT_BITS_FAST(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);

                    if (((flags & 2) == 0) && (pLZ_codes < pLZ_code_buf_end))
                    {
                        flags >>= 1;
                        lit = *pLZ_codes++;
                        MZ_ASSERT(d->m_huff_code_sizes[0][lit]);
                        TDEFL_PUT_BITS_FAST(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);
                    }
                }
            }

            if (pOutput_buf >= d->m_pOutput_buf_end)
                return MZ_FALSE;

            memcpy(pOutput_buf, &bit_buffer, sizeof(mz_uint64));
            pOutput_buf += (bits_in >> 3);
            bit_buffer >>= (bits_in & ~7);
            bits_in &= 7;
        }

#undef TDEFL_PUT_BITS_FAST

        d->m_pOutput_buf = pOutput_buf;
        d->m_bits_in = 0;
        d->m_bit_buffer = 0;

        while (bits_in)
        {
            mz_uint32 n = MZ_MIN(bits_in, 16);
            TDEFL_PUT_BITS((mz_uint)bit_buffer & mz_bitmasks[n], n);
            bit_buffer >>= n;
            bits_in -= n;
        }

        TDEFL_PUT_BITS(d->m_huff_codes[0][256], d->m_huff_code_sizes[0][256]);

        return (d->m_pOutput_buf < d->m_pOutput_buf_end);
    }
#else
static mz_bool tdefl_compress_lz_codes(tdefl_compressor *d)
{
    mz_uint flags;
    mz_uint8 *pLZ_codes;

    flags = 1;
    for (pLZ_codes = d->m_lz_code_buf; pLZ_codes < d->m_pLZ_code_buf; flags >>= 1)
    {
        if (flags == 1)
            flags = *pLZ_codes++ | 0x100;
        if (flags & 1)
        {
            mz_uint sym, num_extra_bits;
            mz_uint match_len = pLZ_codes[0], match_dist = (pLZ_codes[1] | (pLZ_codes[2] << 8));
            pLZ_codes += 3;

            MZ_ASSERT(d->m_huff_code_sizes[0][s_tdefl_len_sym[match_len]]);
            TDEFL_PUT_BITS(d->m_huff_codes[0][s_tdefl_len_sym[match_len]], d->m_huff_code_sizes[0][s_tdefl_len_sym[match_len]]);
            TDEFL_PUT_BITS(match_len & mz_bitmasks[s_tdefl_len_extra[match_len]], s_tdefl_len_extra[match_len]);

            if (match_dist < 512)
            {
                sym = s_tdefl_small_dist_sym[match_dist];
                num_extra_bits = s_tdefl_small_dist_extra[match_dist];
            }
            else
            {
                sym = s_tdefl_large_dist_sym[match_dist >> 8];
                num_extra_bits = s_tdefl_large_dist_extra[match_dist >> 8];
            }
            MZ_ASSERT(d->m_huff_code_sizes[1][sym]);
            TDEFL_PUT_BITS(d->m_huff_codes[1][sym], d->m_huff_code_sizes[1][sym]);
            TDEFL_PUT_BITS(match_dist & mz_bitmasks[num_extra_bits], num_extra_bits);
        }
        else
        {
            mz_uint lit = *pLZ_codes++;
            MZ_ASSERT(d->m_huff_code_sizes[0][lit]);
            TDEFL_PUT_BITS(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);
        }
    }

    TDEFL_PUT_BITS(d->m_huff_codes[0][256], d->m_huff_code_sizes[0][256]);

    return (d->m_pOutput_buf < d->m_pOutput_buf_end);
}
#endif 

    static mz_bool tdefl_compress_block(tdefl_compressor *d, mz_bool static_block)
    {
        if (static_block)
            tdefl_start_static_block(d);
        else
            tdefl_start_dynamic_block(d);
        return tdefl_compress_lz_codes(d);
    }

    static const mz_uint s_tdefl_num_probes[11] = { 0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500 };

    static int tdefl_flush_block(tdefl_compressor *d, int flush)
    {
        mz_uint saved_bit_buf, saved_bits_in;
        mz_uint8 *pSaved_output_buf;
        mz_bool comp_block_succeeded = MZ_FALSE;
        int n, use_raw_block = ((d->m_flags & TDEFL_FORCE_ALL_RAW_BLOCKS) != 0) && (d->m_lookahead_pos - d->m_lz_code_buf_dict_pos) <= d->m_dict_size;
        mz_uint8 *pOutput_buf_start = ((d->m_pPut_buf_func == NULL) && ((*d->m_pOut_buf_size - d->m_out_buf_ofs) >= TDEFL_OUT_BUF_SIZE)) ? ((mz_uint8 *)d->m_pOut_buf + d->m_out_buf_ofs) : d->m_output_buf;

        d->m_pOutput_buf = pOutput_buf_start;
        d->m_pOutput_buf_end = d->m_pOutput_buf + TDEFL_OUT_BUF_SIZE - 16;

        MZ_ASSERT(!d->m_output_flush_remaining);
        d->m_output_flush_ofs = 0;
        d->m_output_flush_remaining = 0;

        *d->m_pLZ_flags = (mz_uint8)(*d->m_pLZ_flags >> d->m_num_flags_left);
        d->m_pLZ_code_buf -= (d->m_num_flags_left == 8);

        if ((d->m_flags & TDEFL_WRITE_ZLIB_HEADER) && (!d->m_block_index))
        {
            const mz_uint8 cmf = 0x78;
            mz_uint8 flg, flevel = 3;
            mz_uint header, i, mz_un = sizeof(s_tdefl_num_probes) / sizeof(mz_uint);

            for (i = 0; i < mz_un; i++)
                if (s_tdefl_num_probes[i] == (d->m_flags & 0xFFF))
                    break;

            if (i < 2)
                flevel = 0;
            else if (i < 6)
                flevel = 1;
            else if (i == 6)
                flevel = 2;

            header = cmf << 8 | (flevel << 6);
            header += 31 - (header % 31);
            flg = header & 0xFF;

            TDEFL_PUT_BITS(cmf, 8);
            TDEFL_PUT_BITS(flg, 8);
        }

        TDEFL_PUT_BITS(flush == TDEFL_FINISH, 1);

        pSaved_output_buf = d->m_pOutput_buf;
        saved_bit_buf = d->m_bit_buffer;
        saved_bits_in = d->m_bits_in;

        if (!use_raw_block)
            comp_block_succeeded = tdefl_compress_block(d, (d->m_flags & TDEFL_FORCE_ALL_STATIC_BLOCKS) || (d->m_total_lz_bytes < 48));

        if (((use_raw_block) || ((d->m_total_lz_bytes) && ((d->m_pOutput_buf - pSaved_output_buf + 1U) >= d->m_total_lz_bytes))) &&
            ((d->m_lookahead_pos - d->m_lz_code_buf_dict_pos) <= d->m_dict_size))
        {
            mz_uint i;
            d->m_pOutput_buf = pSaved_output_buf;
            d->m_bit_buffer = saved_bit_buf, d->m_bits_in = saved_bits_in;
            TDEFL_PUT_BITS(0, 2);
            if (d->m_bits_in)
            {
                TDEFL_PUT_BITS(0, 8 - d->m_bits_in);
            }
            for (i = 2; i; --i, d->m_total_lz_bytes ^= 0xFFFF)
            {
                TDEFL_PUT_BITS(d->m_total_lz_bytes & 0xFFFF, 16);
            }
            for (i = 0; i < d->m_total_lz_bytes; ++i)
            {
                TDEFL_PUT_BITS(d->m_dict[(d->m_lz_code_buf_dict_pos + i) & TDEFL_LZ_DICT_SIZE_MASK], 8);
            }
        }
        else if (!comp_block_succeeded)
        {
            d->m_pOutput_buf = pSaved_output_buf;
            d->m_bit_buffer = saved_bit_buf, d->m_bits_in = saved_bits_in;
            tdefl_compress_block(d, MZ_TRUE);
        }

        if (flush)
        {
            if (flush == TDEFL_FINISH)
            {
                if (d->m_bits_in)
                {
                    TDEFL_PUT_BITS(0, 8 - d->m_bits_in);
                }
                if (d->m_flags & TDEFL_WRITE_ZLIB_HEADER)
                {
                    mz_uint i, a = d->m_adler32;
                    for (i = 0; i < 4; i++)
                    {
                        TDEFL_PUT_BITS((a >> 24) & 0xFF, 8);
                        a <<= 8;
                    }
                }
            }
            else
            {
                mz_uint i, z = 0;
                TDEFL_PUT_BITS(0, 3);
                if (d->m_bits_in)
                {
                    TDEFL_PUT_BITS(0, 8 - d->m_bits_in);
                }
                for (i = 2; i; --i, z ^= 0xFFFF)
                {
                    TDEFL_PUT_BITS(z & 0xFFFF, 16);
                }
            }
        }

        MZ_ASSERT(d->m_pOutput_buf < d->m_pOutput_buf_end);

        memset(&d->m_huff_count[0][0], 0, sizeof(d->m_huff_count[0][0]) * TDEFL_MAX_HUFF_SYMBOLS_0);
        memset(&d->m_huff_count[1][0], 0, sizeof(d->m_huff_count[1][0]) * TDEFL_MAX_HUFF_SYMBOLS_1);

        d->m_pLZ_code_buf = d->m_lz_code_buf + 1;
        d->m_pLZ_flags = d->m_lz_code_buf;
        d->m_num_flags_left = 8;
        d->m_lz_code_buf_dict_pos += d->m_total_lz_bytes;
        d->m_total_lz_bytes = 0;
        d->m_block_index++;

        if ((n = (int)(d->m_pOutput_buf - pOutput_buf_start)) != 0)
        {
            if (d->m_pPut_buf_func)
            {
                *d->m_pIn_buf_size = d->m_pSrc - (const mz_uint8 *)d->m_pIn_buf;
                if (!(*d->m_pPut_buf_func)(d->m_output_buf, n, d->m_pPut_buf_user))
                    return (d->m_prev_return_status = TDEFL_STATUS_PUT_BUF_FAILED);
            }
            else if (pOutput_buf_start == d->m_output_buf)
            {
                int bytes_to_copy = (int)MZ_MIN((size_t)n, (size_t)(*d->m_pOut_buf_size - d->m_out_buf_ofs));
                memcpy((mz_uint8 *)d->m_pOut_buf + d->m_out_buf_ofs, d->m_output_buf, bytes_to_copy);
                d->m_out_buf_ofs += bytes_to_copy;
                if ((n -= bytes_to_copy) != 0)
                {
                    d->m_output_flush_ofs = bytes_to_copy;
                    d->m_output_flush_remaining = n;
                }
            }
            else
            {
                d->m_out_buf_ofs += n;
            }
        }

        return d->m_output_flush_remaining;
    }

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES
#ifdef MINIZ_UNALIGNED_USE_MEMCPY
    static mz_uint16 TDEFL_READ_UNALIGNED_WORD(const mz_uint8 *p)
    {
        mz_uint16 ret;
        memcpy(&ret, p, sizeof(mz_uint16));
        return ret;
    }
    static mz_uint16 TDEFL_READ_UNALIGNED_WORD2(const mz_uint16 *p)
    {
        mz_uint16 ret;
        memcpy(&ret, p, sizeof(mz_uint16));
        return ret;
    }
#else
#define TDEFL_READ_UNALIGNED_WORD(p) *(const mz_uint16 *)(p)
#define TDEFL_READ_UNALIGNED_WORD2(p) *(const mz_uint16 *)(p)
#endif
    static MZ_FORCEINLINE void tdefl_find_match(tdefl_compressor *d, mz_uint lookahead_pos, mz_uint max_dist, mz_uint max_match_len, mz_uint *pMatch_dist, mz_uint *pMatch_len)
    {
        mz_uint dist, pos = lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK, match_len = *pMatch_len, probe_pos = pos, next_probe_pos, probe_len;
        mz_uint num_probes_left = d->m_max_probes[match_len >= 32];
        const mz_uint16 *s = (const mz_uint16 *)(d->m_dict + pos), *p, *q;
        mz_uint16 c01 = TDEFL_READ_UNALIGNED_WORD(&d->m_dict[pos + match_len - 1]), s01 = TDEFL_READ_UNALIGNED_WORD2(s);
        MZ_ASSERT(max_match_len <= TDEFL_MAX_MATCH_LEN);
        if (max_match_len <= match_len)
            return;
        for (;;)
        {
            for (;;)
            {
                if (--num_probes_left == 0)
                    return;
#define TDEFL_PROBE                                                                             \
    next_probe_pos = d->m_next[probe_pos];                                                      \
    if ((!next_probe_pos) || ((dist = (mz_uint16)(lookahead_pos - next_probe_pos)) > max_dist)) \
        return;                                                                                 \
    probe_pos = next_probe_pos & TDEFL_LZ_DICT_SIZE_MASK;                                       \
    if (TDEFL_READ_UNALIGNED_WORD(&d->m_dict[probe_pos + match_len - 1]) == c01)                \
        break;
                TDEFL_PROBE;
                TDEFL_PROBE;
                TDEFL_PROBE;
            }
            if (!dist)
                break;
            q = (const mz_uint16 *)(d->m_dict + probe_pos);
            if (TDEFL_READ_UNALIGNED_WORD2(q) != s01)
                continue;
            p = s;
            probe_len = 32;
            do
            {
            } while ((TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) && (TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) &&
                     (TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) && (TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) && (--probe_len > 0));
            if (!probe_len)
            {
                *pMatch_dist = dist;
                *pMatch_len = MZ_MIN(max_match_len, (mz_uint)TDEFL_MAX_MATCH_LEN);
                break;
            }
            else if ((probe_len = ((mz_uint)(p - s) * 2) + (mz_uint)(*(const mz_uint8 *)p == *(const mz_uint8 *)q)) > match_len)
            {
                *pMatch_dist = dist;
                if ((*pMatch_len = match_len = MZ_MIN(max_match_len, probe_len)) == max_match_len)
                    break;
                c01 = TDEFL_READ_UNALIGNED_WORD(&d->m_dict[pos + match_len - 1]);
            }
        }
    }
#else
static MZ_FORCEINLINE void tdefl_find_match(tdefl_compressor *d, mz_uint lookahead_pos, mz_uint max_dist, mz_uint max_match_len, mz_uint *pMatch_dist, mz_uint *pMatch_len)
{
    mz_uint dist, pos = lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK, match_len = *pMatch_len, probe_pos = pos, next_probe_pos, probe_len;
    mz_uint num_probes_left = d->m_max_probes[match_len >= 32];
    const mz_uint8 *s = d->m_dict + pos, *p, *q;
    mz_uint8 c0 = d->m_dict[pos + match_len], c1 = d->m_dict[pos + match_len - 1];
    MZ_ASSERT(max_match_len <= TDEFL_MAX_MATCH_LEN);
    if (max_match_len <= match_len)
        return;
    for (;;)
    {
        for (;;)
        {
            if (--num_probes_left == 0)
                return;
#define TDEFL_PROBE                                                                               \
    next_probe_pos = d->m_next[probe_pos];                                                        \
    if ((!next_probe_pos) || ((dist = (mz_uint16)(lookahead_pos - next_probe_pos)) > max_dist))   \
        return;                                                                                   \
    probe_pos = next_probe_pos & TDEFL_LZ_DICT_SIZE_MASK;                                         \
    if ((d->m_dict[probe_pos + match_len] == c0) && (d->m_dict[probe_pos + match_len - 1] == c1)) \
        break;
            TDEFL_PROBE;
            TDEFL_PROBE;
            TDEFL_PROBE;
        }
        if (!dist)
            break;
        p = s;
        q = d->m_dict + probe_pos;
        for (probe_len = 0; probe_len < max_match_len; probe_len++)
            if (*p++ != *q++)
                break;
        if (probe_len > match_len)
        {
            *pMatch_dist = dist;
            if ((*pMatch_len = match_len = probe_len) == max_match_len)
                return;
            c0 = d->m_dict[pos + match_len];
            c1 = d->m_dict[pos + match_len - 1];
        }
    }
}
#endif 

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
#ifdef MINIZ_UNALIGNED_USE_MEMCPY
    static mz_uint32 TDEFL_READ_UNALIGNED_WORD32(const mz_uint8 *p)
    {
        mz_uint32 ret;
        memcpy(&ret, p, sizeof(mz_uint32));
        return ret;
    }
#else
#define TDEFL_READ_UNALIGNED_WORD32(p) *(const mz_uint32 *)(p)
#endif
    static mz_bool tdefl_compress_fast(tdefl_compressor *d)
    {
        mz_uint lookahead_pos = d->m_lookahead_pos, lookahead_size = d->m_lookahead_size, dict_size = d->m_dict_size, total_lz_bytes = d->m_total_lz_bytes, num_flags_left = d->m_num_flags_left;
        mz_uint8 *pLZ_code_buf = d->m_pLZ_code_buf, *pLZ_flags = d->m_pLZ_flags;
        mz_uint cur_pos = lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK;

        while ((d->m_src_buf_left) || ((d->m_flush) && (lookahead_size)))
        {
            const mz_uint TDEFL_COMP_FAST_LOOKAHEAD_SIZE = 4096;
            mz_uint dst_pos = (lookahead_pos + lookahead_size) & TDEFL_LZ_DICT_SIZE_MASK;
            mz_uint num_bytes_to_process = (mz_uint)MZ_MIN(d->m_src_buf_left, TDEFL_COMP_FAST_LOOKAHEAD_SIZE - lookahead_size);
            d->m_src_buf_left -= num_bytes_to_process;
            lookahead_size += num_bytes_to_process;

            while (num_bytes_to_process)
            {
                mz_uint32 n = MZ_MIN(TDEFL_LZ_DICT_SIZE - dst_pos, num_bytes_to_process);
                memcpy(d->m_dict + dst_pos, d->m_pSrc, n);
                if (dst_pos < (TDEFL_MAX_MATCH_LEN - 1))
                    memcpy(d->m_dict + TDEFL_LZ_DICT_SIZE + dst_pos, d->m_pSrc, MZ_MIN(n, (TDEFL_MAX_MATCH_LEN - 1) - dst_pos));
                d->m_pSrc += n;
                dst_pos = (dst_pos + n) & TDEFL_LZ_DICT_SIZE_MASK;
                num_bytes_to_process -= n;
            }

            dict_size = MZ_MIN(TDEFL_LZ_DICT_SIZE - lookahead_size, dict_size);
            if ((!d->m_flush) && (lookahead_size < TDEFL_COMP_FAST_LOOKAHEAD_SIZE))
                break;

            while (lookahead_size >= 4)
            {
                mz_uint cur_match_dist, cur_match_len = 1;
                mz_uint8 *pCur_dict = d->m_dict + cur_pos;
                mz_uint first_trigram = TDEFL_READ_UNALIGNED_WORD32(pCur_dict) & 0xFFFFFF;
                mz_uint hash = (first_trigram ^ (first_trigram >> (24 - (TDEFL_LZ_HASH_BITS - 8)))) & TDEFL_LEVEL1_HASH_SIZE_MASK;
                mz_uint probe_pos = d->m_hash[hash];
                d->m_hash[hash] = (mz_uint16)lookahead_pos;

                if (((cur_match_dist = (mz_uint16)(lookahead_pos - probe_pos)) <= dict_size) && ((TDEFL_READ_UNALIGNED_WORD32(d->m_dict + (probe_pos &= TDEFL_LZ_DICT_SIZE_MASK)) & 0xFFFFFF) == first_trigram))
                {
                    const mz_uint16 *p = (const mz_uint16 *)pCur_dict;
                    const mz_uint16 *q = (const mz_uint16 *)(d->m_dict + probe_pos);
                    mz_uint32 probe_len = 32;
                    do
                    {
                    } while ((TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) && (TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) &&
                             (TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) && (TDEFL_READ_UNALIGNED_WORD2(++p) == TDEFL_READ_UNALIGNED_WORD2(++q)) && (--probe_len > 0));
                    cur_match_len = ((mz_uint)(p - (const mz_uint16 *)pCur_dict) * 2) + (mz_uint)(*(const mz_uint8 *)p == *(const mz_uint8 *)q);
                    if (!probe_len)
                        cur_match_len = cur_match_dist ? TDEFL_MAX_MATCH_LEN : 0;

                    if ((cur_match_len < TDEFL_MIN_MATCH_LEN) || ((cur_match_len == TDEFL_MIN_MATCH_LEN) && (cur_match_dist >= 8U * 1024U)))
                    {
                        cur_match_len = 1;
                        *pLZ_code_buf++ = (mz_uint8)first_trigram;
                        *pLZ_flags = (mz_uint8)(*pLZ_flags >> 1);
                        d->m_huff_count[0][(mz_uint8)first_trigram]++;
                    }
                    else
                    {
                        mz_uint32 s0, s1;
                        cur_match_len = MZ_MIN(cur_match_len, lookahead_size);

                        MZ_ASSERT((cur_match_len >= TDEFL_MIN_MATCH_LEN) && (cur_match_dist >= 1) && (cur_match_dist <= TDEFL_LZ_DICT_SIZE));

                        cur_match_dist--;

                        pLZ_code_buf[0] = (mz_uint8)(cur_match_len - TDEFL_MIN_MATCH_LEN);
#ifdef MINIZ_UNALIGNED_USE_MEMCPY
                        memcpy(&pLZ_code_buf[1], &cur_match_dist, sizeof(cur_match_dist));
#else
                        *(mz_uint16 *)(&pLZ_code_buf[1]) = (mz_uint16)cur_match_dist;
#endif
                        pLZ_code_buf += 3;
                        *pLZ_flags = (mz_uint8)((*pLZ_flags >> 1) | 0x80);

                        s0 = s_tdefl_small_dist_sym[cur_match_dist & 511];
                        s1 = s_tdefl_large_dist_sym[cur_match_dist >> 8];
                        d->m_huff_count[1][(cur_match_dist < 512) ? s0 : s1]++;

                        d->m_huff_count[0][s_tdefl_len_sym[cur_match_len - TDEFL_MIN_MATCH_LEN]]++;
                    }
                }
                else
                {
                    *pLZ_code_buf++ = (mz_uint8)first_trigram;
                    *pLZ_flags = (mz_uint8)(*pLZ_flags >> 1);
                    d->m_huff_count[0][(mz_uint8)first_trigram]++;
                }

                if (--num_flags_left == 0)
                {
                    num_flags_left = 8;
                    pLZ_flags = pLZ_code_buf++;
                }

                total_lz_bytes += cur_match_len;
                lookahead_pos += cur_match_len;
                dict_size = MZ_MIN(dict_size + cur_match_len, (mz_uint)TDEFL_LZ_DICT_SIZE);
                cur_pos = (cur_pos + cur_match_len) & TDEFL_LZ_DICT_SIZE_MASK;
                MZ_ASSERT(lookahead_size >= cur_match_len);
                lookahead_size -= cur_match_len;

                if (pLZ_code_buf > &d->m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE - 8])
                {
                    int n;
                    d->m_lookahead_pos = lookahead_pos;
                    d->m_lookahead_size = lookahead_size;
                    d->m_dict_size = dict_size;
                    d->m_total_lz_bytes = total_lz_bytes;
                    d->m_pLZ_code_buf = pLZ_code_buf;
                    d->m_pLZ_flags = pLZ_flags;
                    d->m_num_flags_left = num_flags_left;
                    if ((n = tdefl_flush_block(d, 0)) != 0)
                        return (n < 0) ? MZ_FALSE : MZ_TRUE;
                    total_lz_bytes = d->m_total_lz_bytes;
                    pLZ_code_buf = d->m_pLZ_code_buf;
                    pLZ_flags = d->m_pLZ_flags;
                    num_flags_left = d->m_num_flags_left;
                }
            }

            while (lookahead_size)
            {
                mz_uint8 lit = d->m_dict[cur_pos];

                total_lz_bytes++;
                *pLZ_code_buf++ = lit;
                *pLZ_flags = (mz_uint8)(*pLZ_flags >> 1);
                if (--num_flags_left == 0)
                {
                    num_flags_left = 8;
                    pLZ_flags = pLZ_code_buf++;
                }

                d->m_huff_count[0][lit]++;

                lookahead_pos++;
                dict_size = MZ_MIN(dict_size + 1, (mz_uint)TDEFL_LZ_DICT_SIZE);
                cur_pos = (cur_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK;
                lookahead_size--;

                if (pLZ_code_buf > &d->m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE - 8])
                {
                    int n;
                    d->m_lookahead_pos = lookahead_pos;
                    d->m_lookahead_size = lookahead_size;
                    d->m_dict_size = dict_size;
                    d->m_total_lz_bytes = total_lz_bytes;
                    d->m_pLZ_code_buf = pLZ_code_buf;
                    d->m_pLZ_flags = pLZ_flags;
                    d->m_num_flags_left = num_flags_left;
                    if ((n = tdefl_flush_block(d, 0)) != 0)
                        return (n < 0) ? MZ_FALSE : MZ_TRUE;
                    total_lz_bytes = d->m_total_lz_bytes;
                    pLZ_code_buf = d->m_pLZ_code_buf;
                    pLZ_flags = d->m_pLZ_flags;
                    num_flags_left = d->m_num_flags_left;
                }
            }
        }

        d->m_lookahead_pos = lookahead_pos;
        d->m_lookahead_size = lookahead_size;
        d->m_dict_size = dict_size;
        d->m_total_lz_bytes = total_lz_bytes;
        d->m_pLZ_code_buf = pLZ_code_buf;
        d->m_pLZ_flags = pLZ_flags;
        d->m_num_flags_left = num_flags_left;
        return MZ_TRUE;
    }
#endif 

    static MZ_FORCEINLINE void tdefl_record_literal(tdefl_compressor *d, mz_uint8 lit)
    {
        d->m_total_lz_bytes++;
        *d->m_pLZ_code_buf++ = lit;
        *d->m_pLZ_flags = (mz_uint8)(*d->m_pLZ_flags >> 1);
        if (--d->m_num_flags_left == 0)
        {
            d->m_num_flags_left = 8;
            d->m_pLZ_flags = d->m_pLZ_code_buf++;
        }
        d->m_huff_count[0][lit]++;
    }

    static MZ_FORCEINLINE void tdefl_record_match(tdefl_compressor *d, mz_uint match_len, mz_uint match_dist)
    {
        mz_uint32 s0, s1;

        MZ_ASSERT((match_len >= TDEFL_MIN_MATCH_LEN) && (match_dist >= 1) && (match_dist <= TDEFL_LZ_DICT_SIZE));

        d->m_total_lz_bytes += match_len;

        d->m_pLZ_code_buf[0] = (mz_uint8)(match_len - TDEFL_MIN_MATCH_LEN);

        match_dist -= 1;
        d->m_pLZ_code_buf[1] = (mz_uint8)(match_dist & 0xFF);
        d->m_pLZ_code_buf[2] = (mz_uint8)(match_dist >> 8);
        d->m_pLZ_code_buf += 3;

        *d->m_pLZ_flags = (mz_uint8)((*d->m_pLZ_flags >> 1) | 0x80);
        if (--d->m_num_flags_left == 0)
        {
            d->m_num_flags_left = 8;
            d->m_pLZ_flags = d->m_pLZ_code_buf++;
        }

        s0 = s_tdefl_small_dist_sym[match_dist & 511];
        s1 = s_tdefl_large_dist_sym[(match_dist >> 8) & 127];
        d->m_huff_count[1][(match_dist < 512) ? s0 : s1]++;
        d->m_huff_count[0][s_tdefl_len_sym[match_len - TDEFL_MIN_MATCH_LEN]]++;
    }

    static mz_bool tdefl_compress_normal(tdefl_compressor *d)
    {
        const mz_uint8 *pSrc = d->m_pSrc;
        size_t src_buf_left = d->m_src_buf_left;
        tdefl_flush flush = d->m_flush;

        while ((src_buf_left) || ((flush) && (d->m_lookahead_size)))
        {
            mz_uint len_to_move, cur_match_dist, cur_match_len, cur_pos;
            if ((d->m_lookahead_size + d->m_dict_size) >= (TDEFL_MIN_MATCH_LEN - 1))
            {
                mz_uint dst_pos = (d->m_lookahead_pos + d->m_lookahead_size) & TDEFL_LZ_DICT_SIZE_MASK, ins_pos = d->m_lookahead_pos + d->m_lookahead_size - 2;
                mz_uint hash = (d->m_dict[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] << TDEFL_LZ_HASH_SHIFT) ^ d->m_dict[(ins_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK];
                mz_uint num_bytes_to_process = (mz_uint)MZ_MIN(src_buf_left, TDEFL_MAX_MATCH_LEN - d->m_lookahead_size);
                const mz_uint8 *pSrc_end = pSrc ? pSrc + num_bytes_to_process : NULL;
                src_buf_left -= num_bytes_to_process;
                d->m_lookahead_size += num_bytes_to_process;
                while (pSrc != pSrc_end)
                {
                    mz_uint8 c = *pSrc++;
                    d->m_dict[dst_pos] = c;
                    if (dst_pos < (TDEFL_MAX_MATCH_LEN - 1))
                        d->m_dict[TDEFL_LZ_DICT_SIZE + dst_pos] = c;
                    hash = ((hash << TDEFL_LZ_HASH_SHIFT) ^ c) & (TDEFL_LZ_HASH_SIZE - 1);
                    d->m_next[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] = d->m_hash[hash];
                    d->m_hash[hash] = (mz_uint16)(ins_pos);
                    dst_pos = (dst_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK;
                    ins_pos++;
                }
            }
            else
            {
                while ((src_buf_left) && (d->m_lookahead_size < TDEFL_MAX_MATCH_LEN))
                {
                    mz_uint8 c = *pSrc++;
                    mz_uint dst_pos = (d->m_lookahead_pos + d->m_lookahead_size) & TDEFL_LZ_DICT_SIZE_MASK;
                    src_buf_left--;
                    d->m_dict[dst_pos] = c;
                    if (dst_pos < (TDEFL_MAX_MATCH_LEN - 1))
                        d->m_dict[TDEFL_LZ_DICT_SIZE + dst_pos] = c;
                    if ((++d->m_lookahead_size + d->m_dict_size) >= TDEFL_MIN_MATCH_LEN)
                    {
                        mz_uint ins_pos = d->m_lookahead_pos + (d->m_lookahead_size - 1) - 2;
                        mz_uint hash = ((d->m_dict[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] << (TDEFL_LZ_HASH_SHIFT * 2)) ^ (d->m_dict[(ins_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK] << TDEFL_LZ_HASH_SHIFT) ^ c) & (TDEFL_LZ_HASH_SIZE - 1);
                        d->m_next[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] = d->m_hash[hash];
                        d->m_hash[hash] = (mz_uint16)(ins_pos);
                    }
                }
            }
            d->m_dict_size = MZ_MIN(TDEFL_LZ_DICT_SIZE - d->m_lookahead_size, d->m_dict_size);
            if ((!flush) && (d->m_lookahead_size < TDEFL_MAX_MATCH_LEN))
                break;

            len_to_move = 1;
            cur_match_dist = 0;
            cur_match_len = d->m_saved_match_len ? d->m_saved_match_len : (TDEFL_MIN_MATCH_LEN - 1);
            cur_pos = d->m_lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK;
            if (d->m_flags & (TDEFL_RLE_MATCHES | TDEFL_FORCE_ALL_RAW_BLOCKS))
            {
                if ((d->m_dict_size) && (!(d->m_flags & TDEFL_FORCE_ALL_RAW_BLOCKS)))
                {
                    mz_uint8 c = d->m_dict[(cur_pos - 1) & TDEFL_LZ_DICT_SIZE_MASK];
                    cur_match_len = 0;
                    while (cur_match_len < d->m_lookahead_size)
                    {
                        if (d->m_dict[cur_pos + cur_match_len] != c)
                            break;
                        cur_match_len++;
                    }
                    if (cur_match_len < TDEFL_MIN_MATCH_LEN)
                        cur_match_len = 0;
                    else
                        cur_match_dist = 1;
                }
            }
            else
            {
                tdefl_find_match(d, d->m_lookahead_pos, d->m_dict_size, d->m_lookahead_size, &cur_match_dist, &cur_match_len);
            }
            if (((cur_match_len == TDEFL_MIN_MATCH_LEN) && (cur_match_dist >= 8U * 1024U)) || (cur_pos == cur_match_dist) || ((d->m_flags & TDEFL_FILTER_MATCHES) && (cur_match_len <= 5)))
            {
                cur_match_dist = cur_match_len = 0;
            }
            if (d->m_saved_match_len)
            {
                if (cur_match_len > d->m_saved_match_len)
                {
                    tdefl_record_literal(d, (mz_uint8)d->m_saved_lit);
                    if (cur_match_len >= 128)
                    {
                        tdefl_record_match(d, cur_match_len, cur_match_dist);
                        d->m_saved_match_len = 0;
                        len_to_move = cur_match_len;
                    }
                    else
                    {
                        d->m_saved_lit = d->m_dict[cur_pos];
                        d->m_saved_match_dist = cur_match_dist;
                        d->m_saved_match_len = cur_match_len;
                    }
                }
                else
                {
                    tdefl_record_match(d, d->m_saved_match_len, d->m_saved_match_dist);
                    len_to_move = d->m_saved_match_len - 1;
                    d->m_saved_match_len = 0;
                }
            }
            else if (!cur_match_dist)
                tdefl_record_literal(d, d->m_dict[MZ_MIN(cur_pos, sizeof(d->m_dict) - 1)]);
            else if ((d->m_greedy_parsing) || (d->m_flags & TDEFL_RLE_MATCHES) || (cur_match_len >= 128))
            {
                tdefl_record_match(d, cur_match_len, cur_match_dist);
                len_to_move = cur_match_len;
            }
            else
            {
                d->m_saved_lit = d->m_dict[MZ_MIN(cur_pos, sizeof(d->m_dict) - 1)];
                d->m_saved_match_dist = cur_match_dist;
                d->m_saved_match_len = cur_match_len;
            }
            d->m_lookahead_pos += len_to_move;
            MZ_ASSERT(d->m_lookahead_size >= len_to_move);
            d->m_lookahead_size -= len_to_move;
            d->m_dict_size = MZ_MIN(d->m_dict_size + len_to_move, (mz_uint)TDEFL_LZ_DICT_SIZE);
            if ((d->m_pLZ_code_buf > &d->m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE - 8]) ||
                ((d->m_total_lz_bytes > 31 * 1024) && (((((mz_uint)(d->m_pLZ_code_buf - d->m_lz_code_buf) * 115) >> 7) >= d->m_total_lz_bytes) || (d->m_flags & TDEFL_FORCE_ALL_RAW_BLOCKS))))
            {
                int n;
                d->m_pSrc = pSrc;
                d->m_src_buf_left = src_buf_left;
                if ((n = tdefl_flush_block(d, 0)) != 0)
                    return (n < 0) ? MZ_FALSE : MZ_TRUE;
            }
        }

        d->m_pSrc = pSrc;
        d->m_src_buf_left = src_buf_left;
        return MZ_TRUE;
    }

    static tdefl_status tdefl_flush_output_buffer(tdefl_compressor *d)
    {
        if (d->m_pIn_buf_size)
        {
            *d->m_pIn_buf_size = d->m_pSrc - (const mz_uint8 *)d->m_pIn_buf;
        }

        if (d->m_pOut_buf_size)
        {
            size_t n = MZ_MIN(*d->m_pOut_buf_size - d->m_out_buf_ofs, d->m_output_flush_remaining);
            memcpy((mz_uint8 *)d->m_pOut_buf + d->m_out_buf_ofs, d->m_output_buf + d->m_output_flush_ofs, n);
            d->m_output_flush_ofs += (mz_uint)n;
            d->m_output_flush_remaining -= (mz_uint)n;
            d->m_out_buf_ofs += n;

            *d->m_pOut_buf_size = d->m_out_buf_ofs;
        }

        return (d->m_finished && !d->m_output_flush_remaining) ? TDEFL_STATUS_DONE : TDEFL_STATUS_OKAY;
    }

    tdefl_status tdefl_compress(tdefl_compressor *d, const void *pIn_buf, size_t *pIn_buf_size, void *pOut_buf, size_t *pOut_buf_size, tdefl_flush flush)
    {
        if (!d)
        {
            if (pIn_buf_size)
                *pIn_buf_size = 0;
            if (pOut_buf_size)
                *pOut_buf_size = 0;
            return TDEFL_STATUS_BAD_PARAM;
        }

        d->m_pIn_buf = pIn_buf;
        d->m_pIn_buf_size = pIn_buf_size;
        d->m_pOut_buf = pOut_buf;
        d->m_pOut_buf_size = pOut_buf_size;
        d->m_pSrc = (const mz_uint8 *)(pIn_buf);
        d->m_src_buf_left = pIn_buf_size ? *pIn_buf_size : 0;
        d->m_out_buf_ofs = 0;
        d->m_flush = flush;

        if (((d->m_pPut_buf_func != NULL) == ((pOut_buf != NULL) || (pOut_buf_size != NULL))) || (d->m_prev_return_status != TDEFL_STATUS_OKAY) ||
            (d->m_wants_to_finish && (flush != TDEFL_FINISH)) || (pIn_buf_size && *pIn_buf_size && !pIn_buf) || (pOut_buf_size && *pOut_buf_size && !pOut_buf))
        {
            if (pIn_buf_size)
                *pIn_buf_size = 0;
            if (pOut_buf_size)
                *pOut_buf_size = 0;
            return (d->m_prev_return_status = TDEFL_STATUS_BAD_PARAM);
        }
        d->m_wants_to_finish |= (flush == TDEFL_FINISH);

        if ((d->m_output_flush_remaining) || (d->m_finished))
            return (d->m_prev_return_status = tdefl_flush_output_buffer(d));

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
        if (((d->m_flags & TDEFL_MAX_PROBES_MASK) == 1) &&
            ((d->m_flags & TDEFL_GREEDY_PARSING_FLAG) != 0) &&
            ((d->m_flags & (TDEFL_FILTER_MATCHES | TDEFL_FORCE_ALL_RAW_BLOCKS | TDEFL_RLE_MATCHES)) == 0))
        {
            if (!tdefl_compress_fast(d))
                return d->m_prev_return_status;
        }
        else
#endif
        {
            if (!tdefl_compress_normal(d))
                return d->m_prev_return_status;
        }

        if ((d->m_flags & (TDEFL_WRITE_ZLIB_HEADER | TDEFL_COMPUTE_ADLER32)) && (pIn_buf))
            d->m_adler32 = (mz_uint32)mz_adler32(d->m_adler32, (const mz_uint8 *)pIn_buf, d->m_pSrc - (const mz_uint8 *)pIn_buf);

        if ((flush) && (!d->m_lookahead_size) && (!d->m_src_buf_left) && (!d->m_output_flush_remaining))
        {
            if (tdefl_flush_block(d, flush) < 0)
                return d->m_prev_return_status;
            d->m_finished = (flush == TDEFL_FINISH);
            if (flush == TDEFL_FULL_FLUSH)
            {
                MZ_CLEAR_ARR(d->m_hash);
                MZ_CLEAR_ARR(d->m_next);
                d->m_dict_size = 0;
            }
        }

        return (d->m_prev_return_status = tdefl_flush_output_buffer(d));
    }

    tdefl_status tdefl_compress_buffer(tdefl_compressor *d, const void *pIn_buf, size_t in_buf_size, tdefl_flush flush)
    {
        MZ_ASSERT(d->m_pPut_buf_func);
        return tdefl_compress(d, pIn_buf, &in_buf_size, NULL, NULL, flush);
    }

    tdefl_status tdefl_init(tdefl_compressor *d, tdefl_put_buf_func_ptr pPut_buf_func, void *pPut_buf_user, int flags)
    {
        d->m_pPut_buf_func = pPut_buf_func;
        d->m_pPut_buf_user = pPut_buf_user;
        d->m_flags = (mz_uint)(flags);
        d->m_max_probes[0] = 1 + ((flags & 0xFFF) + 2) / 3;
        d->m_greedy_parsing = (flags & TDEFL_GREEDY_PARSING_FLAG) != 0;
        d->m_max_probes[1] = 1 + (((flags & 0xFFF) >> 2) + 2) / 3;
        if (!(flags & TDEFL_NONDETERMINISTIC_PARSING_FLAG))
            MZ_CLEAR_ARR(d->m_hash);
        d->m_lookahead_pos = d->m_lookahead_size = d->m_dict_size = d->m_total_lz_bytes = d->m_lz_code_buf_dict_pos = d->m_bits_in = 0;
        d->m_output_flush_ofs = d->m_output_flush_remaining = d->m_finished = d->m_block_index = d->m_bit_buffer = d->m_wants_to_finish = 0;
        d->m_pLZ_code_buf = d->m_lz_code_buf + 1;
        d->m_pLZ_flags = d->m_lz_code_buf;
        *d->m_pLZ_flags = 0;
        d->m_num_flags_left = 8;
        d->m_pOutput_buf = d->m_output_buf;
        d->m_pOutput_buf_end = d->m_output_buf;
        d->m_prev_return_status = TDEFL_STATUS_OKAY;
        d->m_saved_match_dist = d->m_saved_match_len = d->m_saved_lit = 0;
        d->m_adler32 = 1;
        d->m_pIn_buf = NULL;
        d->m_pOut_buf = NULL;
        d->m_pIn_buf_size = NULL;
        d->m_pOut_buf_size = NULL;
        d->m_flush = TDEFL_NO_FLUSH;
        d->m_pSrc = NULL;
        d->m_src_buf_left = 0;
        d->m_out_buf_ofs = 0;
        if (!(flags & TDEFL_NONDETERMINISTIC_PARSING_FLAG))
            MZ_CLEAR_ARR(d->m_dict);
        memset(&d->m_huff_count[0][0], 0, sizeof(d->m_huff_count[0][0]) * TDEFL_MAX_HUFF_SYMBOLS_0);
        memset(&d->m_huff_count[1][0], 0, sizeof(d->m_huff_count[1][0]) * TDEFL_MAX_HUFF_SYMBOLS_1);
        return TDEFL_STATUS_OKAY;
    }

    mz_uint32 tdefl_get_adler32(tdefl_compressor *d)
    {
        return d->m_adler32;
    }

    mz_uint tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy)
    {
        mz_uint comp_flags = s_tdefl_num_probes[(level >= 0) ? MZ_MIN(10, level) : MZ_DEFAULT_LEVEL] | ((level <= 3) ? TDEFL_GREEDY_PARSING_FLAG : 0);
        if (window_bits > 0)
            comp_flags |= TDEFL_WRITE_ZLIB_HEADER;

        if (!level)
            comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;
        else if (strategy == MZ_FILTERED)
            comp_flags |= TDEFL_FILTER_MATCHES;
        else if (strategy == MZ_HUFFMAN_ONLY)
            comp_flags &= ~TDEFL_MAX_PROBES_MASK;
        else if (strategy == MZ_FIXED)
            comp_flags |= TDEFL_FORCE_ALL_STATIC_BLOCKS;
        else if (strategy == MZ_RLE)
            comp_flags |= TDEFL_RLE_MATCHES;

        return comp_flags;
    }

#ifdef __cplusplus
}
#endif

#endif 

#ifdef __cplusplus
extern "C"
{
#endif

    

    mz_ulong mz_adler32(mz_ulong adler, const unsigned char *ptr, size_t buf_len)
    {
        mz_uint32 i, s1 = (mz_uint32)(adler & 0xffff), s2 = (mz_uint32)(adler >> 16);
        size_t block_len = buf_len % 5552;
        if (!ptr)
            return MZ_ADLER32_INIT;
        while (buf_len)
        {
            for (i = 0; i + 7 < block_len; i += 8, ptr += 8)
            {
                s1 += ptr[0], s2 += s1;
                s1 += ptr[1], s2 += s1;
                s1 += ptr[2], s2 += s1;
                s1 += ptr[3], s2 += s1;
                s1 += ptr[4], s2 += s1;
                s1 += ptr[5], s2 += s1;
                s1 += ptr[6], s2 += s1;
                s1 += ptr[7], s2 += s1;
            }
            for (; i < block_len; ++i)
                s1 += *ptr++, s2 += s1;
            s1 %= 65521U, s2 %= 65521U;
            buf_len -= block_len;
            block_len = 5552;
        }
        return (s2 << 16) + s1;
    }

    MINIZ_EXPORT void *miniz_def_alloc_func(void *opaque, size_t items, size_t size)
    {
        (void)opaque, (void)items, (void)size;
        return MZ_MALLOC(items * size);
    }
    MINIZ_EXPORT void miniz_def_free_func(void *opaque, void *address)
    {
        (void)opaque, (void)address;
        MZ_FREE(address);
    }
    MINIZ_EXPORT void *miniz_def_realloc_func(void *opaque, void *address, size_t items, size_t size)
    {
        (void)opaque, (void)address, (void)items, (void)size;
        return MZ_REALLOC(address, items * size);
    }

#ifndef MINIZ_NO_ZLIB_APIS

#ifndef MINIZ_NO_DEFLATE_APIS

    int mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy)
    {
        tdefl_compressor *pComp;
        mz_uint comp_flags = TDEFL_COMPUTE_ADLER32 | tdefl_create_comp_flags_from_zip_params(level, window_bits, strategy);

        if (!pStream)
            return MZ_STREAM_ERROR;
        if ((method != MZ_DEFLATED) || ((mem_level < 1) || (mem_level > 9)) || ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS)))
            return MZ_PARAM_ERROR;

        pStream->data_type = 0;
        pStream->adler = MZ_ADLER32_INIT;
        pStream->msg = NULL;
        pStream->reserved = 0;
        pStream->total_in = 0;
        pStream->total_out = 0;
        if (!pStream->zalloc)
            pStream->zalloc = miniz_def_alloc_func;
        if (!pStream->zfree)
            pStream->zfree = miniz_def_free_func;

        pComp = (tdefl_compressor *)pStream->zalloc(pStream->opaque, 1, sizeof(tdefl_compressor));
        if (!pComp)
            return MZ_MEM_ERROR;

        pStream->state = (struct mz_internal_state *)pComp;

        if (tdefl_init(pComp, NULL, NULL, comp_flags) != TDEFL_STATUS_OKAY)
        {
            mz_deflateEnd(pStream);
            return MZ_PARAM_ERROR;
        }

        return MZ_OK;
    }

    int mz_deflate(mz_streamp pStream, int flush)
    {
        size_t in_bytes, out_bytes;
        mz_ulong orig_total_in, orig_total_out;
        int mz_status = MZ_OK;

        if ((!pStream) || (!pStream->state) || (flush < 0) || (flush > MZ_FINISH) || (!pStream->next_out))
            return MZ_STREAM_ERROR;
        if (!pStream->avail_out)
            return MZ_BUF_ERROR;

        if (flush == MZ_PARTIAL_FLUSH)
            flush = MZ_SYNC_FLUSH;

        if (((tdefl_compressor *)pStream->state)->m_prev_return_status == TDEFL_STATUS_DONE)
            return (flush == MZ_FINISH) ? MZ_STREAM_END : MZ_BUF_ERROR;

        orig_total_in = pStream->total_in;
        orig_total_out = pStream->total_out;
        for (;;)
        {
            tdefl_status defl_status;
            in_bytes = pStream->avail_in;
            out_bytes = pStream->avail_out;

            defl_status = tdefl_compress((tdefl_compressor *)pStream->state, pStream->next_in, &in_bytes, pStream->next_out, &out_bytes, (tdefl_flush)flush);
            pStream->next_in += (mz_uint)in_bytes;
            pStream->avail_in -= (mz_uint)in_bytes;
            pStream->total_in += (mz_uint)in_bytes;
            pStream->adler = tdefl_get_adler32((tdefl_compressor *)pStream->state);

            pStream->next_out += (mz_uint)out_bytes;
            pStream->avail_out -= (mz_uint)out_bytes;
            pStream->total_out += (mz_uint)out_bytes;

            if (defl_status < 0)
            {
                mz_status = MZ_STREAM_ERROR;
                break;
            }
            else if (defl_status == TDEFL_STATUS_DONE)
            {
                mz_status = MZ_STREAM_END;
                break;
            }
            else if (!pStream->avail_out)
                break;
            else if ((!pStream->avail_in) && (flush != MZ_FINISH))
            {
                if ((flush) || (pStream->total_in != orig_total_in) || (pStream->total_out != orig_total_out))
                    break;
                return MZ_BUF_ERROR; 
            }
        }
        return mz_status;
    }

    int mz_deflateEnd(mz_streamp pStream)
    {
        if (!pStream)
            return MZ_STREAM_ERROR;
        if (pStream->state)
        {
            pStream->zfree(pStream->opaque, pStream->state);
            pStream->state = NULL;
        }
        return MZ_OK;
    }

    mz_ulong mz_deflateBound(mz_streamp pStream, mz_ulong source_len)
    {
        (void)pStream;
        
        return MZ_MAX(128 + (source_len * 110) / 100, 128 + source_len + ((source_len / (31 * 1024)) + 1) * 5);
    }

#endif 

#ifndef MINIZ_NO_INFLATE_APIS

    typedef struct
    {
        tinfl_decompressor m_decomp;
        mz_uint m_dict_ofs, m_dict_avail, m_first_call, m_has_flushed;
        int m_window_bits;
        mz_uint8 m_dict[TINFL_LZ_DICT_SIZE];
        tinfl_status m_last_status;
    } inflate_state;

    int mz_inflateInit2(mz_streamp pStream, int window_bits)
    {
        inflate_state *pDecomp;
        if (!pStream)
            return MZ_STREAM_ERROR;
        if ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS))
            return MZ_PARAM_ERROR;

        pStream->data_type = 0;
        pStream->adler = 0;
        pStream->msg = NULL;
        pStream->total_in = 0;
        pStream->total_out = 0;
        pStream->reserved = 0;
        if (!pStream->zalloc)
            pStream->zalloc = miniz_def_alloc_func;
        if (!pStream->zfree)
            pStream->zfree = miniz_def_free_func;

        pDecomp = (inflate_state *)pStream->zalloc(pStream->opaque, 1, sizeof(inflate_state));
        if (!pDecomp)
            return MZ_MEM_ERROR;

        pStream->state = (struct mz_internal_state *)pDecomp;

        tinfl_init(&pDecomp->m_decomp);
        pDecomp->m_dict_ofs = 0;
        pDecomp->m_dict_avail = 0;
        pDecomp->m_last_status = TINFL_STATUS_NEEDS_MORE_INPUT;
        pDecomp->m_first_call = 1;
        pDecomp->m_has_flushed = 0;
        pDecomp->m_window_bits = window_bits;

        return MZ_OK;
    }

    int mz_inflate(mz_streamp pStream, int flush)
    {
        inflate_state *pState;
        mz_uint n, first_call, decomp_flags = TINFL_FLAG_COMPUTE_ADLER32;
        size_t in_bytes, out_bytes, orig_avail_in;
        tinfl_status status;

        if ((!pStream) || (!pStream->state))
            return MZ_STREAM_ERROR;
        if (flush == MZ_PARTIAL_FLUSH)
            flush = MZ_SYNC_FLUSH;
        if ((flush) && (flush != MZ_SYNC_FLUSH) && (flush != MZ_FINISH))
            return MZ_STREAM_ERROR;

        pState = (inflate_state *)pStream->state;
        if (pState->m_window_bits > 0)
            decomp_flags |= TINFL_FLAG_PARSE_ZLIB_HEADER;
        orig_avail_in = pStream->avail_in;

        first_call = pState->m_first_call;
        pState->m_first_call = 0;
        if (pState->m_last_status < 0)
            return MZ_DATA_ERROR;

        if (pState->m_has_flushed && (flush != MZ_FINISH))
            return MZ_STREAM_ERROR;
        pState->m_has_flushed |= (flush == MZ_FINISH);

        if ((flush == MZ_FINISH) && (first_call))
        {
            
            decomp_flags |= TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
            in_bytes = pStream->avail_in;
            out_bytes = pStream->avail_out;
            status = tinfl_decompress(&pState->m_decomp, pStream->next_in, &in_bytes, pStream->next_out, pStream->next_out, &out_bytes, decomp_flags);
            pState->m_last_status = status;
            pStream->next_in += (mz_uint)in_bytes;
            pStream->avail_in -= (mz_uint)in_bytes;
            pStream->total_in += (mz_uint)in_bytes;
            pStream->adler = tinfl_get_adler32(&pState->m_decomp);
            pStream->next_out += (mz_uint)out_bytes;
            pStream->avail_out -= (mz_uint)out_bytes;
            pStream->total_out += (mz_uint)out_bytes;

            if (status < 0)
                return MZ_DATA_ERROR;
            else if (status != TINFL_STATUS_DONE)
            {
                pState->m_last_status = TINFL_STATUS_FAILED;
                return MZ_BUF_ERROR;
            }
            return MZ_STREAM_END;
        }
        
        if (flush != MZ_FINISH)
            decomp_flags |= TINFL_FLAG_HAS_MORE_INPUT;

        if (pState->m_dict_avail)
        {
            n = MZ_MIN(pState->m_dict_avail, pStream->avail_out);
            memcpy(pStream->next_out, pState->m_dict + pState->m_dict_ofs, n);
            pStream->next_out += n;
            pStream->avail_out -= n;
            pStream->total_out += n;
            pState->m_dict_avail -= n;
            pState->m_dict_ofs = (pState->m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);
            return ((pState->m_last_status == TINFL_STATUS_DONE) && (!pState->m_dict_avail)) ? MZ_STREAM_END : MZ_OK;
        }

        for (;;)
        {
            in_bytes = pStream->avail_in;
            out_bytes = TINFL_LZ_DICT_SIZE - pState->m_dict_ofs;

            status = tinfl_decompress(&pState->m_decomp, pStream->next_in, &in_bytes, pState->m_dict, pState->m_dict + pState->m_dict_ofs, &out_bytes, decomp_flags);
            pState->m_last_status = status;

            pStream->next_in += (mz_uint)in_bytes;
            pStream->avail_in -= (mz_uint)in_bytes;
            pStream->total_in += (mz_uint)in_bytes;
            pStream->adler = tinfl_get_adler32(&pState->m_decomp);

            pState->m_dict_avail = (mz_uint)out_bytes;

            n = MZ_MIN(pState->m_dict_avail, pStream->avail_out);
            memcpy(pStream->next_out, pState->m_dict + pState->m_dict_ofs, n);
            pStream->next_out += n;
            pStream->avail_out -= n;
            pStream->total_out += n;
            pState->m_dict_avail -= n;
            pState->m_dict_ofs = (pState->m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);

            if (status < 0)
                return MZ_DATA_ERROR; 
            else if ((status == TINFL_STATUS_NEEDS_MORE_INPUT) && (!orig_avail_in))
                return MZ_BUF_ERROR; 
            else if (flush == MZ_FINISH)
            {
                
                if (status == TINFL_STATUS_DONE)
                    return pState->m_dict_avail ? MZ_BUF_ERROR : MZ_STREAM_END;
                
                else if (!pStream->avail_out)
                    return MZ_BUF_ERROR;
            }
            else if ((status == TINFL_STATUS_DONE) || (!pStream->avail_in) || (!pStream->avail_out) || (pState->m_dict_avail))
                break;
        }

        return ((status == TINFL_STATUS_DONE) && (!pState->m_dict_avail)) ? MZ_STREAM_END : MZ_OK;
    }

    int mz_inflateEnd(mz_streamp pStream)
    {
        if (!pStream)
            return MZ_STREAM_ERROR;
        if (pStream->state)
        {
            pStream->zfree(pStream->opaque, pStream->state);
            pStream->state = NULL;
        }
        return MZ_OK;
    }

#endif 

#endif 

#ifdef __cplusplus
}
#endif

#include "microz.h"

#define MICROZ_LEVEL_DEFAULT 6

int microz_init(void)
{
    return MICROZ_OK;
}

int microz_compress_block(const uint8_t *src, uint32_t src_len,
                          uint8_t *dst, uint32_t *dst_len)
{
    mz_stream stream;
    int status;
    uint32_t bound;

    if (src == NULL || dst == NULL || dst_len == NULL) {
        return MICROZ_ERR_ARG;
    }

    bound = microz_compress_bound(src_len);
    memset(&stream, 0, sizeof(stream));
    stream.next_in   = (const unsigned char *)src;
    stream.avail_in  = (unsigned int)src_len;
    stream.next_out  = (unsigned char *)dst;
    stream.avail_out = (unsigned int)bound;

    status = mz_deflateInit2(&stream, MICROZ_LEVEL_DEFAULT,
                             MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
                             9, MZ_DEFAULT_STRATEGY);
    if (status != MZ_OK) {
        return MICROZ_ERR_COMPRESS;
    }

    status = mz_deflate(&stream, MZ_FINISH);
    if (status != MZ_STREAM_END) {
        mz_deflateEnd(&stream);
        return MICROZ_ERR_COMPRESS;
    }

    status = mz_deflateEnd(&stream);
    if (status != MZ_OK) {
        return MICROZ_ERR_COMPRESS;
    }

    *dst_len = (uint32_t)stream.total_out;
    return MICROZ_OK;
}

int microz_decompress_block(const uint8_t *src, uint32_t src_len,
                            uint8_t *dst, uint32_t *dst_len)
{
    mz_stream stream;
    int status;

    if (src == NULL || dst == NULL || dst_len == NULL) {
        return MICROZ_ERR_ARG;
    }

    memset(&stream, 0, sizeof(stream));
    stream.next_in   = (const unsigned char *)src;
    stream.avail_in  = (unsigned int)src_len;
    stream.next_out  = (unsigned char *)dst;
    stream.avail_out = (unsigned int)(*dst_len);

    status = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);
    if (status != MZ_OK) {
        return MICROZ_ERR_DECOMPRESS;
    }

    status = mz_inflate(&stream, MZ_SYNC_FLUSH);
    if (status != MZ_OK && status != MZ_STREAM_END) {
        mz_inflateEnd(&stream);
        return MICROZ_ERR_DECOMPRESS;
    }

    *dst_len = (uint32_t)stream.total_out;

    mz_inflateEnd(&stream);
    return MICROZ_OK;
}

uint32_t microz_compress_bound(uint32_t src_len)
{
    return (uint32_t)mz_deflateBound(NULL, (mz_ulong)src_len);
}

uint32_t microz_compress_work_size(void)
{
    return 0;
}

