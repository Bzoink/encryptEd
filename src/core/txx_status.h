#ifndef TXX_STATUS_H
#define TXX_STATUS_H

typedef enum txx_status {
    TXX_OK = 0,
    TXX_ERR_ARG,
    TXX_ERR_ALLOC,
    TXX_ERR_UTF8,
    TXX_ERR_RANDOM,
    TXX_ERR_COMPRESS,
    TXX_ERR_DECOMPRESS,
    TXX_ERR_FORMAT,
    TXX_ERR_UNSUPPORTED,
    TXX_ERR_AUTH,
    TXX_ERR_IO,
    TXX_ERR_OVERFLOW
} txx_status;

const char *txx_status_string(txx_status status);

#endif