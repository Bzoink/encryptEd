#include "core/txx_status.h"

const char *txx_status_string(txx_status status)
{
    switch (status) {
    case TXX_OK:
        return "ok";
    case TXX_ERR_ARG:
        return "invalid argument";
    case TXX_ERR_ALLOC:
        return "allocation failed";
    case TXX_ERR_UTF8:
        return "invalid UTF-8";
    case TXX_ERR_RANDOM:
        return "random generation failed";
    case TXX_ERR_COMPRESS:
        return "compression failed";
    case TXX_ERR_DECOMPRESS:
        return "decompression failed";
    case TXX_ERR_FORMAT:
        return "invalid TXX format";
    case TXX_ERR_UNSUPPORTED:
        return "unsupported TXX feature";
    case TXX_ERR_AUTH:
        return "authentication failed";
    case TXX_ERR_IO:
        return "I/O failed";
    case TXX_ERR_OVERFLOW:
        return "size overflow";
    default:
        return "unknown error";
    }
}