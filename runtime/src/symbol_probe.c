#include "symbol_probe.h"

#include "compat_bridge.h"

#include <dlfcn.h>
#include <elf.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { HOST_HANDLE_CAPACITY = 8, REPORT_NAME_CAPACITY = 96 };

struct dynamic_symbols {
    const Elf32_Sym *symbols;
    size_t count;
};

struct host_handles {
    void *values[HOST_HANDLE_CAPACITY];
    const char *names[HOST_HANDLE_CAPACITY];
    size_t count;
};

struct name_report {
    const char *values[REPORT_NAME_CAPACITY];
    size_t count;
    size_t dropped;
};

static bool range_in_image(const struct elf32_image *image, uintptr_t address,
                           size_t length)
{
    const uintptr_t start = (uintptr_t)image->mapping;
    const uintptr_t end = start + image->mapping_size;

    return end >= start && address >= start && address <= end &&
           length <= end - address;
}

static int dynamic_symbols(const struct elf32_image *image,
                           struct dynamic_symbols *result,
                           char *error, size_t error_size)
{
    uintptr_t symbol_address = 0U;
    uintptr_t hash_address = 0U;
    size_t symbol_size = 0U;
    size_t index;

    (void)memset(result, 0, sizeof(*result));
    for (index = 0U; index < image->dynamic_count; ++index) {
        const Elf32_Dyn *entry = &image->dynamic[index];

        if (entry->d_tag == DT_NULL) {
            break;
        }
        if (entry->d_tag == DT_SYMTAB) {
            symbol_address = image->load_bias +
                             (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;
        } else if (entry->d_tag == DT_SYMENT) {
            symbol_size = (size_t)(Elf32_Word)entry->d_un.d_val;
        } else if (entry->d_tag == DT_HASH) {
            hash_address = image->load_bias +
                           (uintptr_t)(Elf32_Word)entry->d_un.d_ptr;
        }
    }
    if (symbol_address == 0U || symbol_size != sizeof(Elf32_Sym) ||
        hash_address == 0U ||
        !range_in_image(image, hash_address, 2U * sizeof(uint32_t))) {
        (void)snprintf(error, error_size,
                       "%s has no valid SYSV dynamic symbol table",
                       image->soname);
        return -1;
    }
    result->count = ((const uint32_t *)hash_address)[1];
    if (result->count == 0U ||
        result->count > SIZE_MAX / sizeof(Elf32_Sym) ||
        !range_in_image(image, symbol_address,
                        result->count * sizeof(Elf32_Sym))) {
        (void)snprintf(error, error_size,
                       "%s has an invalid dynamic symbol count",
                       image->soname);
        return -1;
    }
    result->symbols = (const Elf32_Sym *)symbol_address;
    return 0;
}

static bool valid_name(const struct elf32_image *image,
                       const Elf32_Sym *symbol, const char **name)
{
    const char *candidate;

    if (symbol->st_name >= image->string_table_size) {
        return false;
    }
    candidate = image->string_table + symbol->st_name;
    if (memchr(candidate, '\0', image->string_table_size - symbol->st_name) ==
        NULL) {
        return false;
    }
    *name = candidate;
    return true;
}

static bool image_exports(const struct elf32_image *image, const char *name)
{
    struct dynamic_symbols table;
    size_t index;

    if (dynamic_symbols(image, &table, NULL, 0U) != 0) {
        return false;
    }
    for (index = 0U; index < table.count; ++index) {
        const Elf32_Sym *symbol = &table.symbols[index];
        const char *candidate;
        const unsigned int binding = ELF32_ST_BIND(symbol->st_info);

        if (symbol->st_shndx == SHN_UNDEF ||
            (binding != STB_GLOBAL && binding != STB_WEAK) ||
            !valid_name(image, symbol, &candidate)) {
            continue;
        }
        if (strcmp(candidate, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool any_image_exports(const struct elf32_image *images,
                              size_t image_count, const char *name)
{
    size_t index;

    for (index = 0U; index < image_count; ++index) {
        if (image_exports(&images[index], name)) {
            return true;
        }
    }
    return false;
}

static bool in_list(const char *name, const char *const *names, size_t count)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (strcmp(name, names[index]) == 0) {
            return true;
        }
    }
    return false;
}

bool nfsmw_symbol_requires_softfp(const char *name)
{
    static const char *const names[] = {
        "glBlendColor", "glClearColor", "glClearDepthf", "glDepthRangef",
        "glLineWidth", "glPolygonOffset", "glSampleCoverage",
        "glTexParameterf", "glUniform1f", "glUniform2f", "glUniform3f",
        "glUniform4f", "glVertexAttrib1f", "glVertexAttrib2f",
        "glVertexAttrib3f", "glVertexAttrib4f", "acos", "acosf", "asinf",
        "atan2", "atan2f", "ceil", "ceilf", "cos", "cosf", "exp",
        "expf", "floor", "floorf", "fmod", "fmodf", "frexp", "ldexp",
        "log", "log10", "log10f", "lrintf", "modf", "pow", "powf",
        "rint", "roundf", "sin", "sinf", "sqrt", "sqrtf", "strtod",
        "tan", "tanf"
    };

    return in_list(name, names, sizeof(names) / sizeof(names[0]));
}

bool nfsmw_symbol_requires_bionic_bridge(const char *name)
{
    static const char *const exact[] = {
        "__errno", "__sF", "_ctype_", "_tolower_tab_", "closedir",
        "fclose", "fdopen", "fflush", "fopen", "fprintf", "fread",
        "fseek", "fseeko", "ftell", "ftello", "fwide", "fwrite",
        "lstat", "opendir", "readdir", "readdir_r", "sigaction",
        "sigprocmask", "stat", "statfs", "vfprintf", "setjmp", "sigsetjmp", "longjmp",
        "siglongjmp"
    };

    return strncmp(name, "pthread_", 8U) == 0 ||
           strncmp(name, "sem_", 4U) == 0 ||
           strncmp(name, "__pthread_", 10U) == 0 ||
           strncmp(name, "__android_log_", 14U) == 0 ||
           strncmp(name, "AndroidBitmap_", 14U) == 0 ||
           in_list(name, exact, sizeof(exact) / sizeof(exact[0]));
}

static void open_host_handles(struct host_handles *handles)
{
    static const char *const candidates[] = {
        "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
        "libEGL.so.1", "libEGL.so", "libGLESv2.so.2", "libGLESv2.so"
    };
    size_t index;

    (void)memset(handles, 0, sizeof(*handles));
    for (index = 0U;
         index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        void *handle;

        if (handles->count == HOST_HANDLE_CAPACITY) {
            break;
        }
        handle = dlopen(candidates[index], RTLD_LAZY | RTLD_LOCAL);
        if (handle != NULL) {
            handles->values[handles->count] = handle;
            handles->names[handles->count] = candidates[index];
            handles->count += 1U;
        }
    }
}

static void close_host_handles(struct host_handles *handles)
{
    while (handles->count != 0U) {
        handles->count -= 1U;
        (void)dlclose(handles->values[handles->count]);
    }
}

static bool host_exports(const struct host_handles *handles, const char *name)
{
    size_t index;

    if (dlsym(RTLD_DEFAULT, name) != NULL) {
        return true;
    }
    for (index = 0U; index < handles->count; ++index) {
        if (dlsym(handles->values[index], name) != NULL) {
            return true;
        }
    }
    return false;
}

static void report_add(struct name_report *report, const char *name)
{
    size_t index;

    for (index = 0U; index < report->count; ++index) {
        if (strcmp(report->values[index], name) == 0) {
            return;
        }
    }
    if (report->count < REPORT_NAME_CAPACITY) {
        report->values[report->count] = name;
        report->count += 1U;
    } else {
        report->dropped += 1U;
    }
}

static void print_report(const char *label, const struct name_report *report)
{
    size_t index;

    (void)printf("%s (%zu unique):\n", label, report->count + report->dropped);
    for (index = 0U; index < report->count; ++index) {
        (void)printf("  %s\n", report->values[index]);
    }
    if (report->dropped != 0U) {
        (void)printf("  ... %zu additional names omitted\n", report->dropped);
    }
}

int nfsmw_probe_symbols(const struct elf32_image *images, size_t image_count,
                        struct nfsmw_symbol_probe_stats *stats,
                        char *error, size_t error_size)
{
    struct host_handles handles;
    struct name_report bridge_names = { { NULL }, 0U, 0U };
    struct name_report missing_names = { { NULL }, 0U, 0U };
    size_t image_index;

    if (images == NULL || image_count == 0U || stats == NULL) {
        (void)snprintf(error, error_size, "invalid symbol-probe arguments");
        return -1;
    }
    (void)memset(stats, 0, sizeof(*stats));
    open_host_handles(&handles);
    (void)printf("Target host libraries opened (%zu):", handles.count);
    for (image_index = 0U; image_index < handles.count; ++image_index) {
        (void)printf(" %s", handles.names[image_index]);
    }
    (void)printf("\n");

    for (image_index = 0U; image_index < image_count; ++image_index) {
        struct dynamic_symbols table;
        size_t symbol_index;

        if (dynamic_symbols(&images[image_index], &table,
                            error, error_size) != 0) {
            close_host_handles(&handles);
            return -1;
        }
        for (symbol_index = 0U; symbol_index < table.count; ++symbol_index) {
            const Elf32_Sym *symbol = &table.symbols[symbol_index];
            const char *name;
            const unsigned int binding = ELF32_ST_BIND(symbol->st_info);

            if (symbol->st_shndx != SHN_UNDEF || symbol->st_name == 0U ||
                (binding != STB_GLOBAL && binding != STB_WEAK) ||
                !valid_name(&images[image_index], symbol, &name)) {
                continue;
            }
            stats->imports += 1U;
            if (any_image_exports(images, image_count, name)) {
                stats->guest_provided += 1U;
            } else if (nfsmw_symbol_requires_softfp(name)) {
                stats->softfp_thunks += 1U;
            } else if (nfsmw_compat_resolve(name) != 0U) {
                stats->compat_bridges += 1U;
            } else if (nfsmw_symbol_requires_bionic_bridge(name)) {
                stats->abi_bridges_required += 1U;
                report_add(&bridge_names, name);
            } else if (host_exports(&handles, name)) {
                stats->host_candidates += 1U;
            } else if (binding == STB_WEAK) {
                stats->weak_unresolved += 1U;
            } else {
                stats->missing += 1U;
                report_add(&missing_names, name);
            }
        }
    }

    (void)printf("Provider census: imports=%zu guest=%zu softfp=%zu "
                 "compat=%zu host-candidate=%zu unbridged=%zu missing=%zu "
                 "weak=%zu\n",
                 stats->imports, stats->guest_provided, stats->softfp_thunks,
                 stats->compat_bridges, stats->host_candidates,
                 stats->abi_bridges_required, stats->missing,
                 stats->weak_unresolved);
    print_report("Unbridged Bionic/Android APIs", &bridge_names);
    print_report("No target provider found", &missing_names);
    close_host_handles(&handles);
    return 0;
}
