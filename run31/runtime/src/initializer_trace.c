#include "initializer_trace.h"

#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*initializer_function)(void);

static int in_image(const struct elf32_image *image, uintptr_t address,
                    size_t length)
{
    const uintptr_t start = (uintptr_t)image->mapping;
    const uintptr_t end = start + image->mapping_size;

    return end >= start && address >= start && address <= end &&
           length <= end - address;
}

static int call_lifecycle_function(const struct elf32_image *image,
                                   uintptr_t address, const char *phase,
                                   const char *kind, size_t index,
                                   size_t *called,
                                   char *error, size_t error_size)
{
    initializer_function function = NULL;

    if (address == 0U || address == UINT32_MAX) {
        return 0;
    }
    if (!in_image(image, address & ~(uintptr_t)1U, 1U)) {
        (void)snprintf(error, error_size,
                       "%s %s %s[%zu] is outside its image: 0x%08lx",
                       image->soname, phase, kind, index,
                       (unsigned long)address);
        return -1;
    }
    if (sizeof(function) != sizeof(address)) {
        (void)snprintf(error, error_size,
                       "unsupported lifecycle-function pointer representation");
        return -1;
    }
    (void)memcpy(&function, &address, sizeof(function));
    (void)printf("G4-%s enter module=%s kind=%s index=%zu address=0x%08lx\n",
                 phase, image->soname, kind, index, (unsigned long)address);
    function();
    *called += 1U;
    (void)printf("G4-%s leave module=%s kind=%s index=%zu\n",
                 phase, image->soname, kind, index);
    return 0;
}

int nfsmw_run_initializers(const struct elf32_image *image,
                           size_t *called,
                           char *error, size_t error_size)
{
    uintptr_t legacy = 0U;
    uintptr_t array_address = 0U;
    size_t array_size = 0U;
    size_t index;

    if (image == NULL || image->mapping == NULL || called == NULL) {
        (void)snprintf(error, error_size, "invalid initializer arguments");
        return -1;
    }
    for (index = 0U; index < image->dynamic_count; ++index) {
        const Elf32_Dyn *entry = &image->dynamic[index];

        if (entry->d_tag == DT_NULL) {
            break;
        }
        if (entry->d_tag == DT_INIT) {
            legacy = image->load_bias +
                     (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;
        } else if (entry->d_tag == DT_INIT_ARRAY) {
            array_address = image->load_bias +
                            (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;
        } else if (entry->d_tag == DT_INIT_ARRAYSZ) {
            array_size = (size_t)(Elf32_Word)entry->d_un.d_val;
        }
    }
    if (legacy != 0U &&
        call_lifecycle_function(image, legacy, "CTOR", "DT_INIT", 0U,
                                called, error, error_size) != 0) {
        return -1;
    }
    if (array_size == 0U) {
        return 0;
    }
    if (array_address == 0U || array_size % sizeof(Elf32_Addr) != 0U ||
        !in_image(image, array_address, array_size)) {
        (void)snprintf(error, error_size,
                       "invalid DT_INIT_ARRAY in %s", image->soname);
        return -1;
    }
    for (index = 0U; index < array_size / sizeof(Elf32_Addr); ++index) {
        Elf32_Addr address;

        (void)memcpy(&address,
                     (const void *)(array_address + index * sizeof(address)),
                     sizeof(address));
        if (call_lifecycle_function(image, (uintptr_t)address, "CTOR",
                                    "DT_INIT_ARRAY", index, called,
                                    error, error_size) != 0) {
            return -1;
        }
    }
    return 0;
}

int nfsmw_run_finalizers(const struct elf32_image *image,
                         size_t *called,
                         char *error, size_t error_size)
{
    uintptr_t legacy = 0U;
    uintptr_t array_address = 0U;
    size_t array_size = 0U;
    size_t count;
    size_t index;

    if (image == NULL || image->mapping == NULL || called == NULL) {
        (void)snprintf(error, error_size, "invalid finalizer arguments");
        return -1;
    }
    for (index = 0U; index < image->dynamic_count; ++index) {
        const Elf32_Dyn *entry = &image->dynamic[index];

        if (entry->d_tag == DT_NULL) {
            break;
        }
        if (entry->d_tag == DT_FINI) {
            legacy = image->load_bias +
                     (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;
        } else if (entry->d_tag == DT_FINI_ARRAY) {
            array_address = image->load_bias +
                            (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;
        } else if (entry->d_tag == DT_FINI_ARRAYSZ) {
            array_size = (size_t)(Elf32_Word)entry->d_un.d_val;
        }
    }
    if (array_size != 0U) {
        if (array_address == 0U ||
            array_size % sizeof(Elf32_Addr) != 0U ||
            !in_image(image, array_address, array_size)) {
            (void)snprintf(error, error_size,
                           "invalid DT_FINI_ARRAY in %s", image->soname);
            return -1;
        }
        count = array_size / sizeof(Elf32_Addr);
        while (count != 0U) {
            Elf32_Addr address;

            count -= 1U;
            (void)memcpy(&address,
                         (const void *)(array_address +
                                        count * sizeof(address)),
                         sizeof(address));
            if (call_lifecycle_function(image, (uintptr_t)address, "DTOR",
                                        "DT_FINI_ARRAY", count, called,
                                        error, error_size) != 0) {
                return -1;
            }
        }
    }
    if (legacy != 0U &&
        call_lifecycle_function(image, legacy, "DTOR", "DT_FINI", 0U,
                                called, error, error_size) != 0) {
        return -1;
    }
    return 0;
}
