#include "elf32_loader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void set_error(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;

    if (buffer == NULL || size == 0U) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
}

static bool range_in_file(uint64_t offset, uint64_t length, size_t file_size)
{
    return offset <= (uint64_t)file_size &&
           length <= (uint64_t)file_size - offset;
}

static bool range_in_image(const struct elf32_image *image, uintptr_t address,
                           size_t length)
{
    const uintptr_t start = (uintptr_t)image->mapping;
    const uintptr_t end = start + image->mapping_size;

    return end >= start && address >= start && address <= end &&
           length <= end - address;
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1U);
}

static bool align_up(uintptr_t value, uintptr_t alignment, uintptr_t *result)
{
    const uintptr_t mask = alignment - 1U;

    if (value > UINTPTR_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static int protection(Elf32_Word flags)
{
    int result = 0;

    if ((flags & PF_R) != 0U) {
        result |= PROT_READ;
    }
    if ((flags & PF_W) != 0U) {
        result |= PROT_WRITE;
    }
    if ((flags & PF_X) != 0U) {
        result |= PROT_EXEC;
    }
    return result;
}

static int protect_segments(const struct elf32_image *image,
                            char *error, size_t error_size)
{
    size_t index;

    for (index = 0U; index < image->program_header_count; ++index) {
        const Elf32_Phdr *segment = &image->program_headers[index];
        uintptr_t start;
        uintptr_t end;

        if (segment->p_type != PT_LOAD || segment->p_memsz == 0U) {
            continue;
        }
        start = align_down(image->load_bias + (uintptr_t)segment->p_vaddr,
                           image->page_size);
        if (!align_up(image->load_bias + (uintptr_t)segment->p_vaddr +
                      (uintptr_t)segment->p_memsz, image->page_size, &end)) {
            set_error(error, error_size, "segment protection overflow");
            return -1;
        }
        if (mprotect((void *)start, (size_t)(end - start),
                     protection(segment->p_flags)) != 0) {
            set_error(error, error_size, "protect PT_LOAD: %s", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int set_segments_writable(const struct elf32_image *image,
                                 char *error, size_t error_size)
{
    size_t index;

    for (index = 0U; index < image->program_header_count; ++index) {
        const Elf32_Phdr *segment = &image->program_headers[index];
        uintptr_t start;
        uintptr_t end;

        if (segment->p_type != PT_LOAD || segment->p_memsz == 0U) {
            continue;
        }
        start = align_down(image->load_bias + (uintptr_t)segment->p_vaddr,
                           image->page_size);
        if (!align_up(image->load_bias + (uintptr_t)segment->p_vaddr +
                      (uintptr_t)segment->p_memsz, image->page_size, &end)) {
            set_error(error, error_size, "writable segment overflow");
            return -1;
        }
        if (mprotect((void *)start, (size_t)(end - start),
                     PROT_READ | PROT_WRITE) != 0) {
            set_error(error, error_size, "make PT_LOAD writable: %s",
                      strerror(errno));
            return -1;
        }
    }
    return 0;
}

struct elf32_dynamic_info {
    const Elf32_Sym *symbols;
    size_t symbol_count;
    const Elf32_Rel *relocations;
    size_t relocation_count;
    const Elf32_Rel *plt_relocations;
    size_t plt_relocation_count;
};

static int parse_relocation_info(const struct elf32_image *image,
                                 struct elf32_dynamic_info *info,
                                 char *error, size_t error_size)
{
    uintptr_t symbol_address = 0U;
    uintptr_t hash_address = 0U;
    uintptr_t relocation_address = 0U;
    uintptr_t plt_address = 0U;
    size_t symbol_size = 0U;
    size_t relocation_size = 0U;
    size_t relocation_entry_size = sizeof(Elf32_Rel);
    size_t plt_size = 0U;
    Elf32_Sword plt_type = DT_REL;
    size_t index;

    (void)memset(info, 0, sizeof(*info));
    for (index = 0U; index < image->dynamic_count; ++index) {
        const Elf32_Dyn *entry = &image->dynamic[index];
        const uintptr_t pointer =
            image->load_bias + (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;

        if (entry->d_tag == DT_NULL) {
            break;
        }
        switch (entry->d_tag) {
        case DT_SYMTAB:
            symbol_address = pointer;
            break;
        case DT_SYMENT:
            symbol_size = (size_t)(Elf32_Word)entry->d_un.d_val;
            break;
        case DT_HASH:
            hash_address = pointer;
            break;
        case DT_REL:
            relocation_address = pointer;
            break;
        case DT_RELSZ:
            relocation_size = (size_t)(Elf32_Word)entry->d_un.d_val;
            break;
        case DT_RELENT:
            relocation_entry_size =
                (size_t)(Elf32_Word)entry->d_un.d_val;
            break;
        case DT_JMPREL:
            plt_address = pointer;
            break;
        case DT_PLTRELSZ:
            plt_size = (size_t)(Elf32_Word)entry->d_un.d_val;
            break;
        case DT_PLTREL:
            plt_type = (Elf32_Sword)(Elf32_Word)entry->d_un.d_val;
            break;
        default:
            break;
        }
    }
    if (symbol_address == 0U || symbol_size != sizeof(Elf32_Sym) ||
        hash_address == 0U ||
        !range_in_image(image, hash_address, 2U * sizeof(uint32_t))) {
        set_error(error, error_size, "invalid dynamic symbol metadata in %s",
                  image->soname);
        return -1;
    }
    info->symbol_count = ((const uint32_t *)hash_address)[1];
    if (info->symbol_count == 0U ||
        info->symbol_count > SIZE_MAX / sizeof(Elf32_Sym) ||
        !range_in_image(image, symbol_address,
                        info->symbol_count * sizeof(Elf32_Sym))) {
        set_error(error, error_size, "invalid dynamic symbol count in %s",
                  image->soname);
        return -1;
    }
    if (relocation_entry_size != sizeof(Elf32_Rel) ||
        relocation_size % sizeof(Elf32_Rel) != 0U ||
        (relocation_size != 0U &&
         !range_in_image(image, relocation_address, relocation_size))) {
        set_error(error, error_size, "invalid REL table in %s", image->soname);
        return -1;
    }
    if (plt_type != DT_REL || plt_size % sizeof(Elf32_Rel) != 0U ||
        (plt_size != 0U && !range_in_image(image, plt_address, plt_size))) {
        set_error(error, error_size, "invalid PLT REL table in %s",
                  image->soname);
        return -1;
    }
    info->symbols = (const Elf32_Sym *)symbol_address;
    info->relocations = (const Elf32_Rel *)relocation_address;
    info->relocation_count = relocation_size / sizeof(Elf32_Rel);
    info->plt_relocations = (const Elf32_Rel *)plt_address;
    info->plt_relocation_count = plt_size / sizeof(Elf32_Rel);
    return 0;
}

static int relocation_symbol(const struct elf32_image *image,
                             const struct elf32_dynamic_info *info,
                             size_t symbol_index,
                             elf32_symbol_resolver resolver,
                             void *resolver_context,
                             uintptr_t *value,
                             const char **unresolved,
                             char *error, size_t error_size)
{
    const Elf32_Sym *symbol;
    const char *name;
    const unsigned int binding = symbol_index < info->symbol_count ?
        ELF32_ST_BIND(info->symbols[symbol_index].st_info) : STB_LOCAL;

    if (symbol_index >= info->symbol_count) {
        set_error(error, error_size, "relocation symbol index out of range");
        return -1;
    }
    symbol = &info->symbols[symbol_index];
    if (symbol->st_name >= image->string_table_size) {
        set_error(error, error_size, "invalid relocation symbol name");
        return -1;
    }
    name = image->string_table + symbol->st_name;
    if (memchr(name, '\0', image->string_table_size - symbol->st_name) ==
        NULL) {
        set_error(error, error_size, "unterminated relocation symbol name");
        return -1;
    }
    if (symbol->st_shndx != SHN_UNDEF) {
        const unsigned int type = ELF32_ST_TYPE(symbol->st_info);

#ifdef STT_GNU_IFUNC
        if (type == STT_GNU_IFUNC) {
            set_error(error, error_size, "GNU IFUNC is unsupported: %s", name);
            return -1;
        }
#endif
        if (type == STT_TLS) {
            set_error(error, error_size, "TLS symbol is unsupported: %s", name);
            return -1;
        }
        *value = symbol->st_shndx == SHN_ABS ?
            (uintptr_t)symbol->st_value :
            image->load_bias + (uintptr_t)symbol->st_value;
        return 0;
    }
    if (resolver != NULL) {
        *value = resolver(name, binding, resolver_context);
        if (*value != 0U) {
            return 0;
        }
    }
    if (binding == STB_WEAK) {
        *value = 0U;
        return 0;
    }
    *unresolved = name;
    return 1;
}

static int apply_relocations(struct elf32_image *image,
                             const struct elf32_dynamic_info *info,
                             const Elf32_Rel *relocations, size_t count,
                             elf32_symbol_resolver resolver,
                             void *resolver_context,
                             struct elf32_relocation_stats *stats,
                             const char **first_unresolved,
                             char *error, size_t error_size)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        const Elf32_Rel *relocation = &relocations[index];
        const unsigned int type = ELF32_R_TYPE(relocation->r_info);
        const size_t symbol_index = ELF32_R_SYM(relocation->r_info);
        const uintptr_t target_address =
            image->load_bias + (uintptr_t)relocation->r_offset;
        Elf32_Addr addend;
        uintptr_t symbol_value = 0U;
        const char *unresolved = NULL;
        int symbol_result = 0;
        Elf32_Addr value;

        if (!range_in_image(image, target_address, sizeof(value))) {
            set_error(error, error_size,
                      "relocation target outside %s at index %zu",
                      image->soname, index);
            return -1;
        }
        (void)memcpy(&addend, (const void *)target_address, sizeof(addend));
        if (type == R_ARM_RELATIVE) {
            value = (Elf32_Addr)(image->load_bias + (uintptr_t)addend);
            stats->relative += 1U;
        } else if (type == R_ARM_ABS32 || type == R_ARM_GLOB_DAT ||
                   type == R_ARM_JUMP_SLOT) {
            if (type == R_ARM_ABS32) {
                stats->absolute += 1U;
            } else if (type == R_ARM_GLOB_DAT) {
                stats->global_data += 1U;
            } else {
                stats->jump_slots += 1U;
            }
            symbol_result = relocation_symbol(
                image, info, symbol_index, resolver, resolver_context,
                &symbol_value, &unresolved, error, error_size);
            if (symbol_result < 0) {
                return -1;
            }
            if (symbol_result > 0) {
                stats->unresolved += 1U;
                if (*first_unresolved == NULL) {
                    *first_unresolved = unresolved;
                }
                continue;
            }
            value = type == R_ARM_ABS32 ?
                (Elf32_Addr)(symbol_value + (uintptr_t)addend) :
                (Elf32_Addr)symbol_value;
        } else {
            set_error(error, error_size,
                      "unsupported ARM relocation type %u in %s at index %zu",
                      type, image->soname, index);
            return -1;
        }
        (void)memcpy((void *)target_address, &value, sizeof(value));
    }
    return 0;
}

static int parse_dynamic(struct elf32_image *image,
                         char *error, size_t error_size)
{
    uintptr_t string_address = 0U;
    Elf32_Word soname_offset = 0U;
    bool soname_seen = false;
    size_t index;

    for (index = 0U; index < image->dynamic_count; ++index) {
        const Elf32_Dyn *entry = &image->dynamic[index];

        if (entry->d_tag == DT_NULL) {
            break;
        }
        if (entry->d_tag == DT_STRTAB) {
            string_address = image->load_bias + (uintptr_t)entry->d_un.d_ptr;
        } else if (entry->d_tag == DT_STRSZ) {
            image->string_table_size = (size_t)entry->d_un.d_val;
        } else if (entry->d_tag == DT_SONAME) {
            soname_offset = (Elf32_Word)entry->d_un.d_val;
            soname_seen = true;
        } else if (entry->d_tag == DT_RELA || entry->d_tag == DT_RELASZ ||
                   entry->d_tag == DT_RELAENT) {
            set_error(error, error_size,
                      "unexpected RELA relocation table in ARM32 module");
            return -1;
        }
    }
    if (index == image->dynamic_count) {
        set_error(error, error_size, "unterminated PT_DYNAMIC table");
        return -1;
    }
    if (string_address == 0U || image->string_table_size == 0U ||
        !range_in_image(image, string_address, image->string_table_size)) {
        set_error(error, error_size, "invalid dynamic string table");
        return -1;
    }
    image->string_table = (const char *)string_address;
    if (!soname_seen || soname_offset >= image->string_table_size) {
        set_error(error, error_size, "invalid DT_SONAME offset");
        return -1;
    }
    image->soname = image->string_table + soname_offset;
    return 0;
}

int elf32_map(struct elf32_image *image, const char *path,
              char *error, size_t error_size)
{
    int descriptor = -1;
    struct stat status;
    void *file_mapping = MAP_FAILED;
    const Elf32_Ehdr *header;
    const Elf32_Phdr *file_program_headers;
    uintptr_t virtual_minimum = UINTPTR_MAX;
    uintptr_t virtual_maximum = 0U;
    long page_size;
    size_t index;
    int result = -1;

    (void)memset(image, 0, sizeof(*image));
    image->path = path;
    descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        set_error(error, error_size, "open %s: %s", path, strerror(errno));
        goto done;
    }
    if (fstat(descriptor, &status) != 0 || status.st_size <= 0) {
        set_error(error, error_size, "stat %s: %s", path, strerror(errno));
        goto done;
    }
    if ((uintmax_t)status.st_size > SIZE_MAX) {
        set_error(error, error_size, "module is too large: %s", path);
        goto done;
    }
    file_mapping = mmap(NULL, (size_t)status.st_size, PROT_READ, MAP_PRIVATE,
                        descriptor, 0);
    if (file_mapping == MAP_FAILED) {
        set_error(error, error_size, "map input %s: %s", path, strerror(errno));
        goto done;
    }
    if ((size_t)status.st_size < sizeof(Elf32_Ehdr)) {
        set_error(error, error_size, "truncated ELF header: %s", path);
        goto done;
    }
    header = (const Elf32_Ehdr *)file_mapping;
    if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
        header->e_ident[EI_CLASS] != ELFCLASS32 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB ||
        header->e_machine != EM_ARM || header->e_type != ET_DYN ||
        header->e_phentsize != sizeof(Elf32_Phdr) || header->e_phnum == 0U) {
        set_error(error, error_size, "unsupported ELF32/ARM header: %s", path);
        goto done;
    }
    if (!range_in_file(header->e_phoff,
                       (uint64_t)header->e_phnum * sizeof(Elf32_Phdr),
                       (size_t)status.st_size)) {
        set_error(error, error_size, "invalid program-header table: %s", path);
        goto done;
    }
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || ((unsigned long)page_size &
                           ((unsigned long)page_size - 1UL)) != 0UL) {
        set_error(error, error_size, "invalid host page size");
        goto done;
    }
    image->page_size = (size_t)page_size;
    file_program_headers = (const Elf32_Phdr *)
        ((const unsigned char *)file_mapping + header->e_phoff);
    for (index = 0U; index < header->e_phnum; ++index) {
        const Elf32_Phdr *segment = &file_program_headers[index];
        uintptr_t segment_end;

        if (segment->p_type != PT_LOAD) {
            continue;
        }
        if (segment->p_filesz > segment->p_memsz ||
            segment->p_memsz > UINTPTR_MAX - (uintptr_t)segment->p_vaddr ||
            !range_in_file(segment->p_offset, segment->p_filesz,
                           (size_t)status.st_size) ||
            !align_up((uintptr_t)segment->p_vaddr + segment->p_memsz,
                      image->page_size, &segment_end)) {
            set_error(error, error_size, "invalid PT_LOAD segment: %s", path);
            goto done;
        }
        if (align_down(segment->p_vaddr, image->page_size) < virtual_minimum) {
            virtual_minimum = align_down(segment->p_vaddr, image->page_size);
        }
        if (segment_end > virtual_maximum) {
            virtual_maximum = segment_end;
        }
    }
    if (virtual_minimum == UINTPTR_MAX || virtual_maximum <= virtual_minimum) {
        set_error(error, error_size, "ELF has no loadable image: %s", path);
        goto done;
    }
    image->mapping_size = (size_t)(virtual_maximum - virtual_minimum);
    image->mapping = mmap(NULL, image->mapping_size, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image->mapping == MAP_FAILED) {
        image->mapping = NULL;
        set_error(error, error_size, "reserve image %s: %s", path, strerror(errno));
        goto done;
    }
    image->virtual_base = virtual_minimum;
    image->load_bias = (uintptr_t)image->mapping - virtual_minimum;
    for (index = 0U; index < header->e_phnum; ++index) {
        const Elf32_Phdr *segment = &file_program_headers[index];

        if (segment->p_type == PT_LOAD && segment->p_memsz != 0U) {
            const uintptr_t start = image->load_bias +
                align_down(segment->p_vaddr, image->page_size);
            uintptr_t end;

            if (!align_up(image->load_bias + (uintptr_t)segment->p_vaddr +
                          (uintptr_t)segment->p_memsz, image->page_size, &end) ||
                mprotect((void *)start, (size_t)(end - start),
                         PROT_READ | PROT_WRITE) != 0) {
                set_error(error, error_size, "prepare PT_LOAD %zu in %s: %s",
                          index, path, strerror(errno));
                goto done;
            }
            if (segment->p_filesz != 0U) {
                (void)memcpy((void *)(image->load_bias + segment->p_vaddr),
                             (const unsigned char *)file_mapping +
                                 segment->p_offset,
                             segment->p_filesz);
            }
        }
    }

    /* Program headers are copied into the first load segment in these Android
       modules; address them through the mapped image before unmapping input. */
    if (!range_in_image(image, image->load_bias + header->e_phoff,
                        (size_t)header->e_phnum * sizeof(Elf32_Phdr))) {
        set_error(error, error_size, "mapped program headers are unavailable");
        goto done;
    }
    image->program_headers = (const Elf32_Phdr *)
        (image->load_bias + header->e_phoff);
    image->program_header_count = header->e_phnum;
    for (index = 0U; index < image->program_header_count; ++index) {
        const Elf32_Phdr *segment = &image->program_headers[index];

        if (segment->p_type == PT_DYNAMIC) {
            const uintptr_t address = image->load_bias + segment->p_vaddr;
            if (segment->p_memsz < sizeof(Elf32_Dyn) ||
                !range_in_image(image, address, segment->p_memsz)) {
                set_error(error, error_size, "invalid PT_DYNAMIC table");
                goto done;
            }
            image->dynamic = (const Elf32_Dyn *)address;
            image->dynamic_count = segment->p_memsz / sizeof(Elf32_Dyn);
            break;
        }
    }
    if (image->dynamic == NULL || parse_dynamic(image, error, error_size) != 0 ||
        protect_segments(image, error, error_size) != 0) {
        goto done;
    }
    result = 0;

done:
    if (file_mapping != MAP_FAILED) {
        (void)munmap(file_mapping, (size_t)status.st_size);
    }
    if (descriptor >= 0) {
        (void)close(descriptor);
    }
    if (result != 0) {
        elf32_unmap(image);
    }
    return result;
}

int elf32_relocate(struct elf32_image *image, elf32_symbol_resolver resolver,
                   void *resolver_context,
                   struct elf32_relocation_stats *stats,
                   const char **first_unresolved,
                   char *error, size_t error_size)
{
    struct elf32_dynamic_info info;
    struct elf32_relocation_stats local_stats;
    const char *local_first = NULL;
    int result = -1;

    if (image == NULL || image->mapping == NULL) {
        set_error(error, error_size, "invalid relocation arguments");
        return -1;
    }
    if (stats == NULL) {
        stats = &local_stats;
    }
    if (first_unresolved == NULL) {
        first_unresolved = &local_first;
    }
    (void)memset(stats, 0, sizeof(*stats));
    *first_unresolved = NULL;
    if (parse_relocation_info(image, &info, error, error_size) != 0 ||
        set_segments_writable(image, error, error_size) != 0) {
        return -1;
    }
    if (apply_relocations(image, &info, info.relocations,
                          info.relocation_count, resolver, resolver_context,
                          stats, first_unresolved, error, error_size) != 0 ||
        apply_relocations(image, &info, info.plt_relocations,
                          info.plt_relocation_count, resolver,
                          resolver_context, stats, first_unresolved,
                          error, error_size) != 0) {
        goto protect;
    }
    __builtin___clear_cache((char *)image->mapping,
                            (char *)image->mapping + image->mapping_size);
    result = 0;

protect:
    if (protect_segments(image, error, error_size) != 0) {
        return -1;
    }
    return result;
}

uintptr_t elf32_find_export(const struct elf32_image *image, const char *name)
{
    struct elf32_dynamic_info info;
    size_t index;

    if (image == NULL || name == NULL ||
        parse_relocation_info(image, &info, NULL, 0U) != 0) {
        return 0U;
    }
    for (index = 0U; index < info.symbol_count; ++index) {
        const Elf32_Sym *symbol = &info.symbols[index];
        const char *candidate;
        const unsigned int binding = ELF32_ST_BIND(symbol->st_info);

        if (symbol->st_shndx == SHN_UNDEF ||
            (binding != STB_GLOBAL && binding != STB_WEAK) ||
            symbol->st_name >= image->string_table_size) {
            continue;
        }
        candidate = image->string_table + symbol->st_name;
        if (memchr(candidate, '\0',
                   image->string_table_size - symbol->st_name) == NULL ||
            strcmp(candidate, name) != 0) {
            continue;
        }
        if (ELF32_ST_TYPE(symbol->st_info) == STT_TLS) {
            return 0U;
        }
        return symbol->st_shndx == SHN_ABS ?
            (uintptr_t)symbol->st_value :
            image->load_bias + (uintptr_t)symbol->st_value;
    }
    return 0U;
}

void elf32_unmap(struct elf32_image *image)
{
    if (image->mapping != NULL && image->mapping_size != 0U) {
        (void)munmap(image->mapping, image->mapping_size);
    }
    (void)memset(image, 0, sizeof(*image));
}

int elf32_describe(const struct elf32_image *image)
{
    size_t index;
    size_t relative_count = 0U;
    size_t rel_size = 0U;
    size_t plt_size = 0U;

    (void)printf("module=%s soname=%s mapped=%zu bytes bias=0x%08lx\n",
                 image->path, image->soname, image->mapping_size,
                 (unsigned long)image->load_bias);
    for (index = 0U; index < image->dynamic_count; ++index) {
        const Elf32_Dyn *entry = &image->dynamic[index];

        if (entry->d_tag == DT_NULL) {
            break;
        }
        if (entry->d_tag == DT_NEEDED) {
            const size_t string_offset = (size_t)(Elf32_Word)entry->d_un.d_val;

            if (string_offset >= image->string_table_size) {
                (void)fprintf(stderr, "invalid DT_NEEDED string offset\n");
                return -1;
            }
            (void)printf("  needed=%s\n",
                         image->string_table + string_offset);
        } else if (entry->d_tag == DT_RELSZ) {
            rel_size = (size_t)(Elf32_Word)entry->d_un.d_val;
        } else if (entry->d_tag == DT_PLTRELSZ) {
            plt_size = (size_t)(Elf32_Word)entry->d_un.d_val;
#ifdef DT_RELCOUNT
        } else if (entry->d_tag == DT_RELCOUNT) {
            relative_count = (size_t)(Elf32_Word)entry->d_un.d_val;
#endif
        }
    }
    (void)printf("  rel=%zu plt_rel=%zu relative_fastpath=%zu\n",
                 rel_size / sizeof(Elf32_Rel), plt_size / sizeof(Elf32_Rel),
                 relative_count);
    return 0;
}
