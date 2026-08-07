#ifndef MICROZ_H
#define MICROZ_H

#include <stdint.h>

#define MICROZ_OK             0
#define MICROZ_ERR_ARG       -1
#define MICROZ_ERR_COMPRESS  -2
#define MICROZ_ERR_DECOMPRESS -3

int      microz_init(void);
int      microz_compress_block(const uint8_t *src, uint32_t src_len,
                               uint8_t *dst, uint32_t *dst_len);
int      microz_decompress_block(const uint8_t *src, uint32_t src_len,
                                 uint8_t *dst, uint32_t *dst_len);
uint32_t microz_compress_bound(uint32_t src_len);
uint32_t microz_compress_work_size(void);

#endif 
