#ifndef NFSMW_INITIALIZER_TRACE_H
#define NFSMW_INITIALIZER_TRACE_H

#include "elf32_loader.h"

#include <stddef.h>

int nfsmw_run_initializers(const struct elf32_image *image,
                           size_t *called,
                           char *error, size_t error_size);

int nfsmw_run_finalizers(const struct elf32_image *image,
                         size_t *called,
                         char *error, size_t error_size);

#endif
