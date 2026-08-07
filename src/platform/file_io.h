#ifndef TXX_PLATFORM_FILE_IO_H
#define TXX_PLATFORM_FILE_IO_H

#include <stdint.h>

#include "core/txx_buffer.h"
#include "core/txx_status.h"

txx_status txx_read_file_limited(const char *path, uint64_t max_bytes, txx_buffer *buffer);
txx_status txx_write_file_atomic(const char *path, const uint8_t *data, size_t len);

#endif
