#include "common.h"
#include <assert.h>
#include <stdio.h>

void cio_perror(enum CIO_ERROR error)
{
    switch (error)
    {
    case CIO_NO_ERROR:              fprintf(stderr, "CIO: ok\n"); break;
    case CIO_UNKNOWN_ERROR:         fprintf(stderr, "CIO: unknown error"); break;
    case CIO_ALREADY_EXISTS_ERROR:  fprintf(stderr, "CIO: already exists"); break;
    case CIO_ALLOC_ERROR:           fprintf(stderr, "CIO: allocation error"); break;
    case CIO_NOT_FOUND_ERROR:       fprintf(stderr, "CIO: not found"); break;
    case CIO_ERROR_COUNT:           assert(0); break;
    };
}
