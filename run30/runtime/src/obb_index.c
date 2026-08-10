#include "obb_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EOCD_SEARCH = 65557, NAME_LIMIT = 4096 };

struct obb_entry {
    char *name;
    uint32_t size;
    int directory;
};

static struct obb_entry *entries;
static size_t entry_count;
static char archive_path[NAME_LIMIT];
static const char **list_result;
static size_t list_capacity;

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8U));
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static const char *normalize(const char *path)
{
    if (path == NULL) {
        return "";
    }
    while (*path == '/') {
        ++path;
    }
    return path;
}

void nfsmw_obb_close(void)
{
    size_t index;

    for (index = 0U; index < entry_count; ++index) {
        free(entries[index].name);
    }
    free(entries);
    entries = NULL;
    entry_count = 0U;
    free(list_result);
    list_result = NULL;
    list_capacity = 0U;
    archive_path[0] = '\0';
}

int nfsmw_obb_open(const char *path, char *error, size_t error_size)
{
    unsigned char *tail = NULL;
    unsigned char header[46];
    FILE *file = NULL;
    long file_size;
    long tail_size;
    long eocd = -1L;
    uint32_t central_offset;
    uint16_t count;
    size_t index;
    int result = -1;

    nfsmw_obb_close();
    if (path == NULL || path[0] == '\0') {
        (void)snprintf(error, error_size, "OBB path is empty");
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (file_size = ftell(file)) < 22L) {
        (void)snprintf(error, error_size, "cannot open OBB %.200s", path);
        goto done;
    }
    tail_size = file_size < EOCD_SEARCH ? file_size : EOCD_SEARCH;
    tail = malloc((size_t)tail_size);
    if (tail == NULL || fseek(file, file_size - tail_size, SEEK_SET) != 0 ||
        fread(tail, 1U, (size_t)tail_size, file) != (size_t)tail_size) {
        (void)snprintf(error, error_size, "cannot read OBB directory");
        goto done;
    }
    for (eocd = tail_size - 22L; eocd >= 0L; --eocd) {
        if (read_u32(&tail[eocd]) == 0x06054b50U) {
            break;
        }
    }
    if (eocd < 0L) {
        (void)snprintf(error, error_size, "OBB has no ZIP directory");
        goto done;
    }
    count = read_u16(&tail[eocd + 10L]);
    central_offset = read_u32(&tail[eocd + 16L]);
    if (count == UINT16_MAX || central_offset == UINT32_MAX) {
        (void)snprintf(error, error_size, "ZIP64 OBB is not supported");
        goto done;
    }
    entries = calloc((size_t)count, sizeof(*entries));
    if (entries == NULL || fseek(file, (long)central_offset, SEEK_SET) != 0) {
        (void)snprintf(error, error_size, "cannot allocate OBB index");
        goto done;
    }
    for (index = 0U; index < (size_t)count; ++index) {
        uint16_t name_length;
        uint16_t extra_length;
        uint16_t comment_length;
        char *name;

        if (fread(header, 1U, sizeof(header), file) != sizeof(header) ||
            read_u32(header) != 0x02014b50U) {
            (void)snprintf(error, error_size,
                           "invalid OBB entry %zu", index);
            goto done;
        }
        name_length = read_u16(&header[28]);
        extra_length = read_u16(&header[30]);
        comment_length = read_u16(&header[32]);
        if (name_length == 0U || name_length >= NAME_LIMIT) {
            (void)snprintf(error, error_size,
                           "invalid OBB name at %zu", index);
            goto done;
        }
        name = malloc((size_t)name_length + 1U);
        if (name == NULL ||
            fread(name, 1U, name_length, file) != name_length) {
            free(name);
            (void)snprintf(error, error_size, "cannot read OBB name");
            goto done;
        }
        name[name_length] = '\0';
        if (fseek(file, (long)extra_length + (long)comment_length,
                  SEEK_CUR) != 0) {
            free(name);
            (void)snprintf(error, error_size, "invalid OBB metadata");
            goto done;
        }
        entries[index].name = name;
        entries[index].size = read_u32(&header[24]);
        entries[index].directory = name[name_length - 1U] == '/';
        entry_count = index + 1U;
    }
    (void)snprintf(archive_path, sizeof(archive_path), "%s", path);
    (void)printf("G4-OBB PASS path=%s entries=%zu\n",
                 archive_path, entry_count);
    result = 0;

done:
    free(tail);
    if (file != NULL) {
        (void)fclose(file);
    }
    if (result != 0) {
        nfsmw_obb_close();
    }
    return result;
}

const char *nfsmw_obb_path(void)
{
    return archive_path;
}

int64_t nfsmw_obb_asset_size(const char *path)
{
    const char *wanted = normalize(path);
    size_t index;
    size_t length = strlen(wanted);

    while (length > 0U && wanted[length - 1U] == '/') {
        --length;
    }
    for (index = 0U; index < entry_count; ++index) {
        size_t entry_length = strlen(entries[index].name);
        size_t comparable = entry_length;

        if (comparable > 0U && entries[index].name[comparable - 1U] == '/') {
            --comparable;
        }
        if (comparable == length &&
            memcmp(entries[index].name, wanted, length) == 0) {
            return entries[index].directory != 0 ? -1 :
                   (int64_t)entries[index].size;
        }
    }
    return -2;
}

size_t nfsmw_obb_list(const char *directory, const char ***children)
{
    const char *wanted = normalize(directory);
    char prefix[NAME_LIMIT];
    size_t wanted_length = strlen(wanted);
    size_t prefix_length;
    size_t result_count = 0U;
    size_t index;

    while (wanted_length > 0U && wanted[wanted_length - 1U] == '/') {
        --wanted_length;
    }
    if (wanted_length == 0U) {
        prefix[0] = '\0';
        prefix_length = 0U;
    } else {
        (void)memcpy(prefix, wanted, wanted_length);
        prefix[wanted_length] = '/';
        prefix[wanted_length + 1U] = '\0';
        prefix_length = wanted_length + 1U;
    }
    if (list_capacity < entry_count) {
        const char **larger = realloc(list_result,
                                     entry_count * sizeof(*list_result));
        if (larger == NULL) {
            *children = NULL;
            return 0U;
        }
        list_result = larger;
        list_capacity = entry_count;
    }
    for (index = 0U; index < entry_count; ++index) {
        const char *remainder;
        const char *slash;
        size_t candidate_length;
        size_t existing;

        if (strncmp(entries[index].name, prefix, prefix_length) != 0) {
            continue;
        }
        remainder = entries[index].name + prefix_length;
        if (*remainder == '\0') {
            continue;
        }
        slash = strchr(remainder, '/');
        candidate_length = slash != NULL ? (size_t)(slash - remainder) :
                                           strlen(remainder);
        if (candidate_length == 0U) {
            continue;
        }
        for (existing = 0U; existing < result_count; ++existing) {
            const char *existing_slash = strchr(list_result[existing], '/');
            size_t existing_length = existing_slash != NULL ?
                (size_t)(existing_slash - list_result[existing]) :
                strlen(list_result[existing]);
            if (existing_length == candidate_length &&
                memcmp(list_result[existing], remainder,
                       candidate_length) == 0) {
                break;
            }
        }
        if (existing == result_count) {
            list_result[result_count++] = remainder;
            /* Names are returned with the backing ZIP name. The JNI adapter
             * copies only the first path component into a Java string. */
        }
    }
    *children = list_result;
    return result_count;
}
