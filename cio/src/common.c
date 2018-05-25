#include "common.h"
#include <assert.h>
#include <stdio.h>

void cio_perror(enum CIO_ERROR error, const char *message)
{
#define PRINT_ERROR(m, text) \
    do { \
        m ? fprintf(stderr, "CIO: "text": %s\n", m) : fprintf(stderr, "CIO: "text"\n"); \
    } while (0)

    switch (error)
    {
    case CIO_NO_ERROR:              PRINT_ERROR(message, "ok"); break;
    case CIO_UNKNOWN_ERROR:         PRINT_ERROR(message, "unknown error"); break;
    case CIO_ALREADY_EXISTS_ERROR:  PRINT_ERROR(message, "already exists"); break;
    case CIO_ALLOC_ERROR:           PRINT_ERROR(message, "allocation error"); break;
    case CIO_NOT_FOUND_ERROR:       PRINT_ERROR(message, "not found"); break;
    case CIO_WRITE_ERROR:           PRINT_ERROR(message, "write error"); break;
    case CIO_ERROR_COUNT:           assert(0); break;
    };

#undef PRINT_ERROR
}

int timeMsFromTv(struct timeval *tv)
{
    return tv->tv_sec*1000 + tv->tv_usec/1000;
}
