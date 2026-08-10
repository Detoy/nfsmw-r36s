#include "relocation_probe.h"

#include "compat_bridge.h"
#include "softfp_symbols.h"
#include "symbol_probe.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RELOCATION_HOST_CAPACITY = 8 };

struct relocation_context {
    struct elf32_image *images;
    size_t current_image;
    void *host_handles[RELOCATION_HOST_CAPACITY];
    size_t host_count;
    struct nfsmw_relocation_probe_stats *stats;
};

static void *nfsmw_dso_handle;
static void *retained_host_handles[RELOCATION_HOST_CAPACITY];
static size_t retained_host_count;

static void nfsmw_android_assert2(const char *file, int line,
                                  const char *function, const char *message)
{
    (void)fprintf(stderr, "Android assertion at %s:%d (%s): %s\n",
                  file != NULL ? file : "unknown", line,
                  function != NULL ? function : "unknown",
                  message != NULL ? message : "no message");
    abort();
}

static uintptr_t pointer_value(void *pointer)
{
    uintptr_t result = 0U;

    (void)memcpy(&result, &pointer, sizeof(result));
    return result;
}

static uintptr_t alias_lookup(const char *name)
{
    if (strcmp(name, "__assert2") == 0) {
        typedef void (*assert_function)(const char *, int, const char *,
                                        const char *);
        assert_function function = nfsmw_android_assert2;
        uintptr_t result = 0U;

        if (sizeof(function) == sizeof(result)) {
            (void)memcpy(&result, &function, sizeof(result));
        }
        return result;
    }
    if (strcmp(name, "__dso_handle") == 0) {
        return (uintptr_t)&nfsmw_dso_handle;
    }
    return 0U;
}

static void open_host_libraries(struct relocation_context *context)
{
    static const char *const candidates[] = {
        "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
        "libEGL.so.1", "libEGL.so", "libGLESv2.so.2", "libGLESv2.so"
    };
    size_t index;

    for (index = 0U;
         index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        void *handle;

        if (context->host_count == RELOCATION_HOST_CAPACITY) {
            break;
        }
        handle = dlopen(candidates[index], RTLD_LAZY | RTLD_LOCAL);
        if (handle != NULL) {
            context->host_handles[context->host_count] = handle;
            context->host_count += 1U;
        }
    }
}

static void close_host_libraries(struct relocation_context *context)
{
    while (context->host_count != 0U) {
        context->host_count -= 1U;
        (void)dlclose(context->host_handles[context->host_count]);
    }
}

void nfsmw_relocation_release_hosts(void)
{
    while (retained_host_count != 0U) {
        retained_host_count -= 1U;
        (void)dlclose(retained_host_handles[retained_host_count]);
        retained_host_handles[retained_host_count] = NULL;
    }
}

static void retain_host_libraries(struct relocation_context *context)
{
    size_t index;

    nfsmw_relocation_release_hosts();
    for (index = 0U; index < context->host_count; ++index) {
        retained_host_handles[index] = context->host_handles[index];
        context->host_handles[index] = NULL;
    }
    retained_host_count = context->host_count;
    context->host_count = 0U;
}

static uintptr_t host_lookup(const struct relocation_context *context,
                             const char *name)
{
    void *address = dlsym(RTLD_DEFAULT, name);
    size_t index;

    if (address != NULL) {
        return pointer_value(address);
    }
    for (index = 0U; index < context->host_count; ++index) {
        address = dlsym(context->host_handles[index], name);
        if (address != NULL) {
            return pointer_value(address);
        }
    }
    return 0U;
}

static uintptr_t relocation_lookup(const char *name, unsigned int binding,
                                   void *opaque)
{
    struct relocation_context *context = opaque;
    uintptr_t address;
    size_t index;

    (void)binding;
    for (index = 0U; index < context->current_image; ++index) {
        address = elf32_find_export(&context->images[index], name);
        if (address != 0U) {
            context->stats->guest_resolutions += 1U;
            return address;
        }
    }
    if (nfsmw_symbol_requires_softfp(name)) {
        address = nfsmw_softfp_resolve(name);
        if (address != 0U) {
            context->stats->softfp_resolutions += 1U;
            return address;
        }
    }
    address = alias_lookup(name);
    if (address != 0U) {
        context->stats->alias_resolutions += 1U;
        return address;
    }
    address = nfsmw_compat_resolve(name);
    if (address != 0U) {
        context->stats->alias_resolutions += 1U;
        return address;
    }
    if (nfsmw_symbol_requires_bionic_bridge(name)) {
        context->stats->blocked_resolutions += 1U;
        return 0U;
    }
    address = host_lookup(context, name);
    if (address != 0U) {
        context->stats->host_resolutions += 1U;
        return address;
    }
    context->stats->missing_resolutions += 1U;
    return 0U;
}

int nfsmw_relocation_probe(struct elf32_image *images, size_t image_count,
                           struct nfsmw_relocation_probe_stats *stats,
                           char *error, size_t error_size)
{
    struct relocation_context context;
    size_t index;

    if (images == NULL || image_count == 0U || stats == NULL) {
        (void)snprintf(error, error_size,
                       "invalid relocation-probe arguments");
        return -1;
    }
    (void)memset(stats, 0, sizeof(*stats));
    (void)memset(&context, 0, sizeof(context));
    context.images = images;
    context.stats = stats;
    open_host_libraries(&context);

    for (index = 0U; index < image_count; ++index) {
        struct elf32_relocation_stats module_stats;
        const char *first_unresolved = NULL;
        size_t module_total;

        context.current_image = index;
        if (elf32_relocate(&images[index], relocation_lookup, &context,
                           &module_stats, &first_unresolved,
                           error, error_size) != 0) {
            close_host_libraries(&context);
            return -1;
        }
        module_total = module_stats.relative + module_stats.absolute +
                       module_stats.global_data + module_stats.jump_slots;
        stats->total_relocations += module_total;
        stats->unresolved_relocations += module_stats.unresolved;
        (void)printf("G3-REL %s total=%zu relative=%zu abs32=%zu glob=%zu "
                     "jump=%zu unresolved=%zu first=%s\n",
                     images[index].soname, module_total,
                     module_stats.relative, module_stats.absolute,
                     module_stats.global_data, module_stats.jump_slots,
                     module_stats.unresolved,
                     first_unresolved != NULL ? first_unresolved : "none");
    }
    retain_host_libraries(&context);
    (void)printf("G3-REL providers guest=%zu softfp=%zu host=%zu alias=%zu "
                 "blocked=%zu missing=%zu\n",
                 stats->guest_resolutions, stats->softfp_resolutions,
                 stats->host_resolutions, stats->alias_resolutions,
                 stats->blocked_resolutions, stats->missing_resolutions);
    if (stats->unresolved_relocations == 0U) {
        (void)printf("G3 FULL RELOCATION PASS processed=%zu unresolved=0; "
                     "host providers retained\n",
                     stats->total_relocations);
    } else {
        (void)printf("G3-REL PHASE-A PASS processed=%zu unresolved=%zu; "
                     "host providers retained\n",
                     stats->total_relocations,
                     stats->unresolved_relocations);
    }
    return 0;
}
