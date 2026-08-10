#ifndef NFSMW_ELF32_LOADER_H
#define NFSMW_ELF32_LOADER_H

#include <elf.h>
#include <stddef.h>
#include <stdint.h>

enum { ELF32_LOADER_ERROR_CAPACITY = 512 };

struct elf32_image {
    void *mapping;
    size_t mapping_size;
    uintptr_t load_bias;
    uintptr_t virtual_base;
    size_t page_size;
    const Elf32_Phdr *program_headers;
    size_t program_header_count;
    const Elf32_Dyn *dynamic;
    size_t dynamic_count;
    const char *string_table;
    size_t string_table_size;
    const char *soname;
    const char *path;
};

struct elf32_relocation_stats {
    size_t relative;
    size_t absolute;
    size_t global_data;
    size_t jump_slots;
    size_t unresolved;
};

typedef uintptr_t (*elf32_symbol_resolver)(const char *name,
                                           unsigned int binding,
                                           void *context);

int elf32_map(struct elf32_image *image, const char *path,
              char *error, size_t error_size);
void elf32_unmap(struct elf32_image *image);
int elf32_describe(const struct elf32_image *image);
int elf32_relocate(struct elf32_image *image, elf32_symbol_resolver resolver,
                   void *resolver_context,
                   struct elf32_relocation_stats *stats,
                   const char **first_unresolved,
                   char *error, size_t error_size);
uintptr_t elf32_find_export(const struct elf32_image *image, const char *name);

#endif
