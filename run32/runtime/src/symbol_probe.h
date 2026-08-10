#ifndef NFSMW_SYMBOL_PROBE_H
#define NFSMW_SYMBOL_PROBE_H

#include "elf32_loader.h"

#include <stddef.h>
#include <stdbool.h>

struct nfsmw_symbol_probe_stats {
    size_t imports;
    size_t guest_provided;
    size_t softfp_thunks;
    size_t compat_bridges;
    size_t host_candidates;
    size_t abi_bridges_required;
    size_t missing;
    size_t weak_unresolved;
};

int nfsmw_probe_symbols(const struct elf32_image *images, size_t image_count,
                        struct nfsmw_symbol_probe_stats *stats,
                        char *error, size_t error_size);
bool nfsmw_symbol_requires_softfp(const char *name);
bool nfsmw_symbol_requires_bionic_bridge(const char *name);

#endif
