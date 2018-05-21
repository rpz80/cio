#if !defined(CIO_COMMON_H)
#define CIO_COMMON_H

enum CIO_ERROR {
    CIO_NO_ERROR,
    CIO_UNKNOWN_ERROR,
    CIO_ALREADY_EXISTS_ERROR,
    CIO_ALLOC_ERROR,
    CIO_NOT_FOUND_ERROR,
    CIO_WRITE_ERROR,
    
    CIO_ERROR_COUNT
};

void cio_perror(enum CIO_ERROR error, const char *message);

enum CIO_FLAGS {
    CIO_FLAG_IN = 1,
    CIO_FLAG_OUT = 2,
    CIO_FLAG_ERR = 4,
    CIO_FLAG_RDHUP = 8
};

typedef void (*pollset_cb_t)(void *ctx, int fd, int flags);

#endif
