#ifndef NFSMW_RELOCATION_PROBE_H
#define NFSMW_RELOCATION_PROBE_H

#include "elf32_loader.h"

#include <stddef.h>

struct nfsmw_relocation_probe_stats {
    size_t guest_resolutions;
    size_t softfp_resolutions;
    size_t host_resolutions;
    size_t alias_resolutions;
    size_t blocked_resolutions;
    size_t missing_resolutions;
    size_t total_relocations;
    size_t unresolved_relocations;
};

int nfsmw_relocation_probe(struct elf32_image *images, size_t image_count,
                           struct nfsmw_relocation_probe_stats *stats,
                           char *error, size_t error_size);

void nfsmw_relocation_release_hosts(void);

#endif
