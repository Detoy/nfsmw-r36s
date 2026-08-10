#include "compat_bridge.h"
#include "jni_bridge.h"
#include "opensl_bridge.h"

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#if UINTPTR_MAX != UINT32_MAX
#error "The NFS MW Bionic compatibility bridge requires a 32-bit host"
#endif

enum { BIONIC_FILE_SIZE = 84, BIONIC_DIRENT_NAME = 256 };

enum { ATEXIT_CAPACITY = 512 };

struct bionic_sigaction {
    void (*handler)(int);
    uint32_t mask;
    int32_t flags;
    void (*restorer)(void);
};

_Static_assert(sizeof(struct bionic_sigaction) == 16U,
               "ARM Bionic sigaction layout must be 16 bytes");

enum { BIONIC_SA_RESTORER = 0x04000000 };

struct compat_atexit_entry {
    void (*destructor)(void *);
    void *argument;
    void *dso;
    bool active;
};

static unsigned char bionic_files[3][BIONIC_FILE_SIZE];
static char ctype_storage[257];
static const char *ctype_pointer = &ctype_storage[1];
static int16_t tolower_storage[257];
static const int16_t *tolower_pointer = &tolower_storage[1];
static struct compat_atexit_entry atexit_entries[ATEXIT_CAPACITY];
static size_t atexit_count;
static pthread_mutex_t atexit_lock = PTHREAD_MUTEX_INITIALIZER;

static void *compat_dlopen(const char *name, int flags)
{
    if (name != NULL &&
        (strcmp(name, "libOpenSLES.so") == 0 ||
         strcmp(name, "libOpenSLES.so.1") == 0)) {
        (void)printf("G8-OPENSL intercepted dlopen(%s)\n", name);
        return nfsmw_opensl_handle();
    }
    return dlopen(name, flags);
}

static void *compat_dlsym(void *handle, const char *name)
{
    return nfsmw_opensl_is_handle(handle) != 0 ?
           nfsmw_opensl_dlsym(name) : dlsym(handle, name);
}

static int compat_dlclose(void *handle)
{
    return nfsmw_opensl_is_handle(handle) != 0 ? 0 : dlclose(handle);
}

struct bionic_timespec32 {
    int32_t seconds;
    int32_t nanoseconds;
};

struct bionic_timeval32 {
    int32_t seconds;
    int32_t microseconds;
};

struct bionic_timezone32 {
    int32_t minutes_west;
    int32_t daylight_saving;
};

static int compat_clock_gettime(clockid_t clock_id,
                                struct bionic_timespec32 *guest)
{
    struct timespec host;
    int result;

    if (guest == NULL) {
        errno = EFAULT;
        return -1;
    }
    result = clock_gettime(clock_id, &host);
    if (result != 0) return result;
    if (host.tv_sec < INT32_MIN || host.tv_sec > INT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    guest->seconds = (int32_t)host.tv_sec;
    guest->nanoseconds = (int32_t)host.tv_nsec;
    return 0;
}

static int compat_gettimeofday(struct bionic_timeval32 *guest,
                               struct bionic_timezone32 *zone)
{
    struct timeval host;
    int result;

    if (guest == NULL) {
        errno = EFAULT;
        return -1;
    }
    result = gettimeofday(&host, NULL);
    if (result != 0) return result;
    if (host.tv_sec < INT32_MIN || host.tv_sec > INT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    guest->seconds = (int32_t)host.tv_sec;
    guest->microseconds = (int32_t)host.tv_usec;
    if (zone != NULL) {
        zone->minutes_west = 0;
        zone->daylight_saving = 0;
    }
    return 0;
}

struct bionic_stat32 {
    uint64_t device;
    unsigned char padding0[4];
    uint32_t legacy_inode;
    uint32_t mode;
    uint32_t links;
    uint32_t user;
    uint32_t group;
    uint64_t special_device;
    unsigned char padding3[4];
    int64_t size;
    uint32_t block_size;
    uint64_t blocks;
    struct bionic_timespec32 access_time;
    struct bionic_timespec32 modification_time;
    struct bionic_timespec32 change_time;
    uint64_t inode;
};

struct bionic_statfs32 {
    uint32_t type;
    uint32_t block_size;
    uint32_t blocks;
    uint32_t blocks_free;
    uint32_t blocks_available;
    uint32_t files;
    uint32_t files_free;
    int32_t fsid[2];
    uint32_t name_length;
    uint32_t fragment_size;
    uint32_t flags;
    uint32_t spare[4];
};

struct bionic_dirent32 {
    uint64_t inode;
    int64_t offset;
    uint16_t record_length;
    uint8_t type;
    char name[BIONIC_DIRENT_NAME];
};

struct bionic_pthread_attr32 {
    uint32_t flags;
    void *stack_base;
    uint32_t stack_size;
    uint32_t guard_size;
    int32_t scheduling_policy;
    int32_t scheduling_priority;
};

_Static_assert(sizeof(struct bionic_stat32) == 104U,
               "unexpected Android ARM stat layout");
_Static_assert(sizeof(struct bionic_dirent32) == 280U,
               "unexpected Android ARM dirent layout");
_Static_assert(sizeof(struct bionic_pthread_attr32) == 24U,
               "unexpected Android ARM pthread_attr layout");

static _Thread_local struct bionic_dirent32 directory_entry;

static FILE *host_stream(void *guest)
{
    const uintptr_t address = (uintptr_t)guest;
    const uintptr_t base = (uintptr_t)&bionic_files[0][0];
    const uintptr_t end = base + sizeof(bionic_files);

    if (address >= base && address < end) {
        const size_t index = (size_t)(address - base) / BIONIC_FILE_SIZE;

        if (index == 0U) {
            return stdin;
        }
        if (index == 1U) {
            return stdout;
        }
        return stderr;
    }
    return (FILE *)guest;
}

static int compat_fclose(void *stream) { return fclose(host_stream(stream)); }
static int compat_fflush(void *stream) { return fflush(host_stream(stream)); }
static size_t compat_fread(void *buffer, size_t size, size_t count,
                           void *stream)
{
    return fread(buffer, size, count, host_stream(stream));
}
static size_t compat_fwrite(const void *buffer, size_t size, size_t count,
                            void *stream)
{
    return fwrite(buffer, size, count, host_stream(stream));
}
static int compat_fseek(void *stream, int32_t offset, int origin)
{
    return fseek(host_stream(stream), (long)offset, origin);
}
static int compat_fseeko(void *stream, int64_t offset, int origin)
{
    if (offset < INT32_MIN || offset > INT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    return fseek(host_stream(stream), (long)offset, origin);
}
static int32_t compat_ftell(void *stream)
{
    return (int32_t)ftell(host_stream(stream));
}
static int64_t compat_ftello(void *stream)
{
    return (int64_t)ftell(host_stream(stream));
}
static int compat_fwide(void *stream, int mode)
{
    return fwide(host_stream(stream), mode);
}
static void *compat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}
static void *compat_fdopen(int descriptor, const char *mode)
{
    return fdopen(descriptor, mode);
}
static int compat_vfprintf(void *stream, const char *format, va_list arguments)
{
    return vfprintf(host_stream(stream), format, arguments);
}
static int compat_fprintf(void *stream, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = compat_vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int *compat_errno(void) { return &errno; }

static void __attribute__((noreturn)) compat_abort(void)
{
    const uintptr_t caller =
        (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));

    (void)fprintf(stderr, "G-ABORT guest-return=0x%08lx\n",
                  (unsigned long)caller);
    (void)fflush(NULL);
    (void)raise(SIGABRT);
    _exit(134);
}

static void __attribute__((noreturn))
compat_assert2(const char *file, int line, const char *function,
               const char *condition)
{
    (void)fprintf(stderr, "G-ASSERT %s:%d %s: %s\n",
                  file != NULL ? file : "unknown", line,
                  function != NULL ? function : "unknown",
                  condition != NULL ? condition : "unknown");
    compat_abort();
}

static int compat_android_log_vprint(int priority, const char *tag,
                                     const char *format, va_list arguments)
{
    int result;

    (void)fprintf(stderr, "android[%d] %s: ", priority,
                  tag != NULL ? tag : "NFSMW");
    result = vfprintf(stderr, format != NULL ? format : "", arguments);
    (void)fputc('\n', stderr);
    return result;
}

static int compat_android_log_print(int priority, const char *tag,
                                    const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = compat_android_log_vprint(priority, tag, format, arguments);
    va_end(arguments);
    return result;
}

static int compat_android_log_write(int priority, const char *tag,
                                    const char *text)
{
    return compat_android_log_print(priority, tag, "%s",
                                    text != NULL ? text : "");
}

static void compat_android_log_assert(const char *condition, const char *tag,
                                      const char *format, ...)
{
    va_list arguments;

    (void)fprintf(stderr, "Android assertion (%s) %s: ",
                  condition != NULL ? condition : "none",
                  tag != NULL ? tag : "NFSMW");
    va_start(arguments, format);
    (void)vfprintf(stderr, format != NULL ? format : "", arguments);
    va_end(arguments);
    (void)fputc('\n', stderr);
    abort();
}

static int compat_android_bitmap_get_info(void *environment, void *bitmap,
                                          void *information)
{
    (void)environment;
    return nfsmw_jni_bitmap_info(bitmap, information);
}

static int compat_android_bitmap_lock(void *environment, void *bitmap,
                                      void **pixels)
{
    (void)environment;
    return nfsmw_jni_bitmap_lock(bitmap, pixels);
}

static int compat_android_bitmap_unlock(void *environment, void *bitmap)
{
    (void)environment;
    return nfsmw_jni_bitmap_unlock(bitmap);
}

static int compat_cxa_atexit(void (*destructor)(void *), void *argument,
                             void *dso)
{
    int result = -1;

    (void)pthread_mutex_lock(&atexit_lock);
    if (destructor != NULL && atexit_count < ATEXIT_CAPACITY) {
        atexit_entries[atexit_count].destructor = destructor;
        atexit_entries[atexit_count].argument = argument;
        atexit_entries[atexit_count].dso = dso;
        atexit_entries[atexit_count].active = true;
        atexit_count += 1U;
        result = 0;
    }
    (void)pthread_mutex_unlock(&atexit_lock);
    return result;
}

static void compat_cxa_finalize(void *dso)
{
    size_t index;

    (void)pthread_mutex_lock(&atexit_lock);
    index = atexit_count;
    while (index != 0U) {
        struct compat_atexit_entry entry;

        index -= 1U;
        if (!atexit_entries[index].active ||
            (dso != NULL && atexit_entries[index].dso != dso)) {
            continue;
        }
        entry = atexit_entries[index];
        atexit_entries[index].active = false;
        (void)pthread_mutex_unlock(&atexit_lock);
        entry.destructor(entry.argument);
        (void)pthread_mutex_lock(&atexit_lock);
    }
    (void)pthread_mutex_unlock(&atexit_lock);
}

static int compat_cxa_thread_atexit(void (*destructor)(void *),
                                    void *argument, void *dso)
{
    return compat_cxa_atexit(destructor, argument, dso);
}

static void translate_stat(struct bionic_stat32 *guest,
                           const struct stat *host)
{
    (void)memset(guest, 0, sizeof(*guest));
    guest->device = (uint64_t)host->st_dev;
    guest->legacy_inode = (uint32_t)host->st_ino;
    guest->mode = (uint32_t)host->st_mode;
    guest->links = (uint32_t)host->st_nlink;
    guest->user = (uint32_t)host->st_uid;
    guest->group = (uint32_t)host->st_gid;
    guest->special_device = (uint64_t)host->st_rdev;
    guest->size = (int64_t)host->st_size;
    guest->block_size = (uint32_t)host->st_blksize;
    guest->blocks = (uint64_t)host->st_blocks;
    guest->access_time.seconds = (int32_t)host->st_atim.tv_sec;
    guest->access_time.nanoseconds = (int32_t)host->st_atim.tv_nsec;
    guest->modification_time.seconds = (int32_t)host->st_mtim.tv_sec;
    guest->modification_time.nanoseconds = (int32_t)host->st_mtim.tv_nsec;
    guest->change_time.seconds = (int32_t)host->st_ctim.tv_sec;
    guest->change_time.nanoseconds = (int32_t)host->st_ctim.tv_nsec;
    guest->inode = (uint64_t)host->st_ino;
}

static int compat_stat(const char *path, struct bionic_stat32 *guest)
{
    struct stat host;
    const int result = stat(path, &host);

    if (result == 0 && guest != NULL) {
        translate_stat(guest, &host);
    }
    return result;
}

static int compat_statfs(const char *path, struct bionic_statfs32 *guest)
{
    struct statfs host;
    const int result = statfs(path, &host);

    if (result == 0 && guest != NULL) {
        (void)memset(guest, 0, sizeof(*guest));
        guest->type = (uint32_t)host.f_type;
        guest->block_size = (uint32_t)host.f_bsize;
        guest->blocks = (uint32_t)host.f_blocks;
        guest->blocks_free = (uint32_t)host.f_bfree;
        guest->blocks_available = (uint32_t)host.f_bavail;
        guest->files = (uint32_t)host.f_files;
        guest->files_free = (uint32_t)host.f_ffree;
        (void)memcpy(guest->fsid, &host.f_fsid, sizeof(guest->fsid));
        guest->name_length = (uint32_t)host.f_namelen;
        guest->fragment_size = (uint32_t)host.f_frsize;
        guest->flags = (uint32_t)host.f_flags;
    }
    return result;
}

static void translate_dirent(struct bionic_dirent32 *guest,
                             const struct dirent *host)
{
    (void)memset(guest, 0, sizeof(*guest));
    guest->inode = (uint64_t)host->d_ino;
    guest->offset = (int64_t)host->d_off;
    guest->record_length = (uint16_t)sizeof(*guest);
    guest->type = host->d_type;
    (void)snprintf(guest->name, sizeof(guest->name), "%s", host->d_name);
}

static void *compat_opendir(const char *path) { return opendir(path); }
static int compat_closedir(void *directory) { return closedir(directory); }
static struct bionic_dirent32 *compat_readdir(void *directory)
{
    struct dirent *entry = readdir(directory);

    if (entry == NULL) {
        return NULL;
    }
    translate_dirent(&directory_entry, entry);
    return &directory_entry;
}

static int compat_readdir_r(void *directory, struct bionic_dirent32 *entry,
                            struct bionic_dirent32 **result)
{
    struct dirent *host;

    errno = 0;
    host = readdir(directory);
    if (host == NULL) {
        *result = NULL;
        return errno;
    }
    translate_dirent(entry, host);
    *result = entry;
    return 0;
}

static uintptr_t guest_slot_value(const void *guest)
{
    uintptr_t value = 0U;

    (void)memcpy(&value, guest, sizeof(value));
    return value;
}

static void set_guest_slot(void *guest, const void *value)
{
    (void)memcpy(guest, &value, sizeof(value));
}

static int mutex_type_from_guest(const void *attribute)
{
    int32_t type = 0;

    if (attribute != NULL) {
        (void)memcpy(&type, attribute, sizeof(type));
    }
    if (type == 1) {
        return PTHREAD_MUTEX_RECURSIVE;
    }
    if (type == 2) {
        return PTHREAD_MUTEX_ERRORCHECK;
    }
    return PTHREAD_MUTEX_NORMAL;
}

static pthread_mutex_t *compat_mutex_get(void *guest, bool create,
                                         const void *attribute)
{
    uintptr_t raw = guest_slot_value(guest);
    pthread_mutex_t *host;

    if (raw > UINT32_C(0x0000ffff)) {
        return (pthread_mutex_t *)raw;
    }
    if (!create) {
        return NULL;
    }
    host = calloc(1U, sizeof(*host));
    if (host != NULL) {
        pthread_mutexattr_t host_attribute;
        int type = mutex_type_from_guest(attribute);

        if (raw != 0U) {
            const unsigned int encoded =
                (unsigned int)((raw >> 14U) & UINT32_C(3));
            type = encoded == 1U ? PTHREAD_MUTEX_RECURSIVE :
                   encoded == 2U ? PTHREAD_MUTEX_ERRORCHECK : type;
        }
        (void)pthread_mutexattr_init(&host_attribute);
        (void)pthread_mutexattr_settype(&host_attribute, type);
        if (pthread_mutex_init(host, &host_attribute) != 0) {
            free(host);
            host = NULL;
        }
        (void)pthread_mutexattr_destroy(&host_attribute);
        if (host != NULL) {
            set_guest_slot(guest, host);
        }
    }
    return host;
}

static int compat_pthread_mutex_init(void *guest, const void *attribute)
{
    set_guest_slot(guest, NULL);
    return compat_mutex_get(guest, true, attribute) != NULL ? 0 : ENOMEM;
}
static int compat_pthread_mutex_destroy(void *guest)
{
    pthread_mutex_t *host = compat_mutex_get(guest, false, NULL);
    int result = 0;

    if (host != NULL) {
        result = pthread_mutex_destroy(host);
        if (result == 0) {
            free(host);
            set_guest_slot(guest, NULL);
        }
    }
    return result;
}
static int compat_pthread_mutex_lock(void *guest)
{
    pthread_mutex_t *host = compat_mutex_get(guest, true, NULL);
    return host != NULL ? pthread_mutex_lock(host) : ENOMEM;
}
static int compat_pthread_mutex_unlock(void *guest)
{
    pthread_mutex_t *host = compat_mutex_get(guest, false, NULL);
    return host != NULL ? pthread_mutex_unlock(host) : EINVAL;
}
static int compat_pthread_mutex_trylock(void *guest)
{
    pthread_mutex_t *host = compat_mutex_get(guest, true, NULL);
    return host != NULL ? pthread_mutex_trylock(host) : ENOMEM;
}

static pthread_cond_t *compat_cond_get(void *guest, bool create)
{
    uintptr_t raw = guest_slot_value(guest);
    pthread_cond_t *host;

    if (raw > UINT32_C(0x0000ffff)) {
        return (pthread_cond_t *)raw;
    }
    if (!create) {
        return NULL;
    }
    host = calloc(1U, sizeof(*host));
    if (host != NULL && pthread_cond_init(host, NULL) != 0) {
        free(host);
        host = NULL;
    }
    if (host != NULL) {
        set_guest_slot(guest, host);
    }
    return host;
}

static int compat_pthread_cond_init(void *guest, const void *attribute)
{
    (void)attribute;
    set_guest_slot(guest, NULL);
    return compat_cond_get(guest, true) != NULL ? 0 : ENOMEM;
}
static int compat_pthread_cond_destroy(void *guest)
{
    pthread_cond_t *host = compat_cond_get(guest, false);
    int result = 0;

    if (host != NULL) {
        result = pthread_cond_destroy(host);
        if (result == 0) {
            free(host);
            set_guest_slot(guest, NULL);
        }
    }
    return result;
}
static int compat_pthread_cond_signal(void *guest)
{
    pthread_cond_t *host = compat_cond_get(guest, true);
    return host != NULL ? pthread_cond_signal(host) : ENOMEM;
}
static int compat_pthread_cond_broadcast(void *guest)
{
    pthread_cond_t *host = compat_cond_get(guest, true);
    return host != NULL ? pthread_cond_broadcast(host) : ENOMEM;
}
static int compat_pthread_cond_wait(void *condition, void *mutex)
{
    pthread_cond_t *host_condition = compat_cond_get(condition, true);
    pthread_mutex_t *host_mutex = compat_mutex_get(mutex, true, NULL);
    return host_condition != NULL && host_mutex != NULL ?
        pthread_cond_wait(host_condition, host_mutex) : ENOMEM;
}
static int compat_pthread_cond_timedwait(
    void *condition, void *mutex, const struct bionic_timespec32 *timeout)
{
    pthread_cond_t *host_condition = compat_cond_get(condition, true);
    pthread_mutex_t *host_mutex = compat_mutex_get(mutex, true, NULL);
    struct timespec host_timeout;

    if (host_condition == NULL || host_mutex == NULL || timeout == NULL) {
        return EINVAL;
    }
    host_timeout.tv_sec = (time_t)timeout->seconds;
    host_timeout.tv_nsec = (long)timeout->nanoseconds;
    return pthread_cond_timedwait(host_condition, host_mutex, &host_timeout);
}

static pthread_rwlock_t *compat_rwlock_get(void *guest, bool create)
{
    uintptr_t raw = guest_slot_value(guest);
    pthread_rwlock_t *host;

    if (raw > UINT32_C(0x0000ffff)) {
        return (pthread_rwlock_t *)raw;
    }
    if (!create) {
        return NULL;
    }
    host = calloc(1U, sizeof(*host));
    if (host != NULL && pthread_rwlock_init(host, NULL) != 0) {
        free(host);
        host = NULL;
    }
    if (host != NULL) {
        set_guest_slot(guest, host);
    }
    return host;
}

static int compat_pthread_rwlock_init(void *guest, const void *attribute)
{
    (void)attribute;
    (void)memset(guest, 0, 40U);
    return compat_rwlock_get(guest, true) != NULL ? 0 : ENOMEM;
}
static int compat_pthread_rwlock_destroy(void *guest)
{
    pthread_rwlock_t *host = compat_rwlock_get(guest, false);
    int result = 0;
    if (host != NULL) {
        result = pthread_rwlock_destroy(host);
        if (result == 0) {
            free(host);
            (void)memset(guest, 0, 40U);
        }
    }
    return result;
}
static int compat_pthread_rwlock_rdlock(void *guest)
{
    pthread_rwlock_t *host = compat_rwlock_get(guest, true);
    return host != NULL ? pthread_rwlock_rdlock(host) : ENOMEM;
}
static int compat_pthread_rwlock_wrlock(void *guest)
{
    pthread_rwlock_t *host = compat_rwlock_get(guest, true);
    return host != NULL ? pthread_rwlock_wrlock(host) : ENOMEM;
}
static int compat_pthread_rwlock_unlock(void *guest)
{
    pthread_rwlock_t *host = compat_rwlock_get(guest, false);
    return host != NULL ? pthread_rwlock_unlock(host) : EINVAL;
}

static sem_t *compat_sem_get(void *guest, bool create, unsigned int value)
{
    uintptr_t raw = guest_slot_value(guest);
    sem_t *host;

    if (raw > UINT32_C(0x0000ffff)) {
        return (sem_t *)raw;
    }
    if (!create) {
        return NULL;
    }
    host = calloc(1U, sizeof(*host));
    if (host != NULL && sem_init(host, 0, value) != 0) {
        free(host);
        host = NULL;
    }
    if (host != NULL) {
        set_guest_slot(guest, host);
    }
    return host;
}

static int compat_sem_init(void *guest, int shared, unsigned int value)
{
    if (shared != 0) {
        return -1;
    }
    set_guest_slot(guest, NULL);
    return compat_sem_get(guest, true, value) != NULL ? 0 : -1;
}
static int compat_sem_destroy(void *guest)
{
    sem_t *host = compat_sem_get(guest, false, 0U);
    int result = 0;
    if (host != NULL) {
        result = sem_destroy(host);
        if (result == 0) {
            free(host);
            set_guest_slot(guest, NULL);
        }
    }
    return result;
}
static int compat_sem_wait(void *guest)
{
    sem_t *host = compat_sem_get(guest, true, 0U);
    return host != NULL ? sem_wait(host) : -1;
}
static int compat_sem_post(void *guest)
{
    sem_t *host = compat_sem_get(guest, true, 0U);
    return host != NULL ? sem_post(host) : -1;
}
static int compat_sem_trywait(void *guest)
{
    sem_t *host = compat_sem_get(guest, true, 0U);
    return host != NULL ? sem_trywait(host) : -1;
}
static int compat_sem_getvalue(void *guest, int *value)
{
    sem_t *host = compat_sem_get(guest, true, 0U);
    return host != NULL ? sem_getvalue(host, value) : -1;
}
static int compat_sem_timedwait(void *guest,
                                const struct bionic_timespec32 *timeout)
{
    sem_t *host = compat_sem_get(guest, true, 0U);
    struct timespec host_timeout;
    if (host == NULL || timeout == NULL) {
        return -1;
    }
    host_timeout.tv_sec = (time_t)timeout->seconds;
    host_timeout.tv_nsec = (long)timeout->nanoseconds;
    return sem_timedwait(host, &host_timeout);
}

static int compat_pthread_attr_init(struct bionic_pthread_attr32 *attribute)
{
    (void)memset(attribute, 0, sizeof(*attribute));
    attribute->stack_size = 1024U * 1024U;
    return 0;
}
static int compat_pthread_attr_destroy(struct bionic_pthread_attr32 *attribute)
{
    (void)attribute;
    return 0;
}
static int compat_pthread_attr_setstacksize(
    struct bionic_pthread_attr32 *attribute, uint32_t size)
{
    attribute->stack_size = size;
    return 0;
}
static int compat_pthread_attr_setstack(struct bionic_pthread_attr32 *attribute,
                                        void *base, uint32_t size)
{
    attribute->stack_base = base;
    attribute->stack_size = size;
    return 0;
}
static int compat_pthread_attr_getstack(
    const struct bionic_pthread_attr32 *attribute, void **base, uint32_t *size)
{
    if (base != NULL) {
        *base = attribute->stack_base;
    }
    if (size != NULL) {
        *size = attribute->stack_size;
    }
    return 0;
}
static int compat_pthread_attr_setschedparam(
    struct bionic_pthread_attr32 *attribute, const struct sched_param *parameter)
{
    attribute->scheduling_priority = parameter->sched_priority;
    return 0;
}
static int compat_pthread_attr_setschedpolicy(
    struct bionic_pthread_attr32 *attribute, int policy)
{
    attribute->scheduling_policy = policy;
    return 0;
}
static int compat_pthread_attr_setdetachstate(
    struct bionic_pthread_attr32 *attribute, int state)
{
    if (state != 0) {
        attribute->flags |= 1U;
    } else {
        attribute->flags &= ~1U;
    }
    return 0;
}

static int host_attr(const struct bionic_pthread_attr32 *guest,
                     pthread_attr_t *host)
{
    int result = pthread_attr_init(host);
    if (result != 0 || guest == NULL) {
        return result;
    }
    if (guest->stack_base != NULL && guest->stack_size != 0U) {
        result = pthread_attr_setstack(host, guest->stack_base,
                                       guest->stack_size);
    } else if (guest->stack_size != 0U) {
        result = pthread_attr_setstacksize(host, guest->stack_size);
    }
    if (result == 0 && (guest->flags & 1U) != 0U) {
        result = pthread_attr_setdetachstate(host, PTHREAD_CREATE_DETACHED);
    }
    return result;
}

static int compat_pthread_create(uint32_t *thread,
                                 const struct bionic_pthread_attr32 *attribute,
                                 void *(*routine)(void *), void *argument)
{
    pthread_attr_t host_attribute;
    pthread_t host_thread;
    int result = host_attr(attribute, &host_attribute);

    if (result == 0) {
        result = pthread_create(&host_thread, &host_attribute,
                                routine, argument);
    }
    (void)pthread_attr_destroy(&host_attribute);
    if (result == 0) {
        *thread = (uint32_t)host_thread;
    }
    return result;
}

static uint32_t compat_pthread_self(void)
{
    return (uint32_t)pthread_self();
}
static int compat_pthread_equal(uint32_t left, uint32_t right)
{
    return pthread_equal((pthread_t)left, (pthread_t)right);
}
static int compat_pthread_join(uint32_t thread, void **result)
{
    return pthread_join((pthread_t)thread, result);
}
static int compat_pthread_detach(uint32_t thread)
{
    return pthread_detach((pthread_t)thread);
}
static void compat_pthread_exit(void *result) { pthread_exit(result); }
static int compat_pthread_setschedparam(uint32_t thread, int policy,
                                        const struct sched_param *parameter)
{
    return pthread_setschedparam((pthread_t)thread, policy, parameter);
}
static int compat_pthread_getschedparam(uint32_t thread, int *policy,
                                        struct sched_param *parameter)
{
    return pthread_getschedparam((pthread_t)thread, policy, parameter);
}
static int compat_pthread_getattr_np(uint32_t thread,
                                     struct bionic_pthread_attr32 *guest)
{
    pthread_attr_t host;
    void *base = NULL;
    size_t size = 0U;
    int result = pthread_getattr_np((pthread_t)thread, &host);

    if (result == 0) {
        (void)compat_pthread_attr_init(guest);
        (void)pthread_attr_getstack(&host, &base, &size);
        guest->stack_base = base;
        guest->stack_size = (uint32_t)size;
        (void)pthread_attr_destroy(&host);
    }
    return result;
}

static int compat_pthread_once(int32_t *once,
                               void (*initialization)(void))
{
    return pthread_once((pthread_once_t *)once, initialization);
}
static int compat_pthread_key_create(uint32_t *key,
                                     void (*destructor)(void *))
{
    pthread_key_t host_key;
    const int result = pthread_key_create(&host_key, destructor);
    if (result == 0) {
        *key = (uint32_t)host_key;
    }
    return result;
}
static int compat_pthread_key_delete(uint32_t key)
{
    return pthread_key_delete((pthread_key_t)key);
}
static int compat_pthread_setspecific(uint32_t key, const void *value)
{
    return pthread_setspecific((pthread_key_t)key, value);
}
static void *compat_pthread_getspecific(uint32_t key)
{
    return pthread_getspecific((pthread_key_t)key);
}

static int compat_mutexattr_init(int32_t *attribute)
{
    *attribute = 0;
    return 0;
}
static int compat_mutexattr_destroy(int32_t *attribute)
{
    (void)attribute;
    return 0;
}
static int compat_mutexattr_settype(int32_t *attribute, int type)
{
    *attribute = type;
    return 0;
}
static int compat_mutexattr_setpshared(int32_t *attribute, int shared)
{
    (void)attribute;
    return shared == 0 ? 0 : ENOTSUP;
}

static int compat_sigaction(int signal_number, const void *action,
                            void *old_action)
{
    const struct bionic_sigaction *guest_action = action;
    struct bionic_sigaction *guest_old_action = old_action;
    struct sigaction host_action;
    struct sigaction host_old_action;
    struct sigaction *host_action_pointer = NULL;
    struct sigaction *host_old_action_pointer = NULL;
    int index;
    int result;

    if (guest_action != NULL) {
        (void)memset(&host_action, 0, sizeof(host_action));
        host_action.sa_handler = guest_action->handler;
        host_action.sa_flags =
            guest_action->flags & ~BIONIC_SA_RESTORER;
        (void)sigemptyset(&host_action.sa_mask);
        for (index = 1; index <= 32; ++index) {
            if ((guest_action->mask &
                 (UINT32_C(1) << (unsigned int)(index - 1))) != 0U) {
                (void)sigaddset(&host_action.sa_mask, index);
            }
        }
        host_action_pointer = &host_action;
    }
    if (guest_old_action != NULL) {
        host_old_action_pointer = &host_old_action;
    }
    result = sigaction(signal_number, host_action_pointer,
                       host_old_action_pointer);
    if (result == 0 && guest_old_action != NULL) {
        guest_old_action->handler = host_old_action.sa_handler;
        guest_old_action->mask = 0U;
        for (index = 1; index <= 32; ++index) {
            if (sigismember(&host_old_action.sa_mask, index) == 1) {
                guest_old_action->mask |=
                    UINT32_C(1) << (unsigned int)(index - 1);
            }
        }
        guest_old_action->flags = host_old_action.sa_flags;
        guest_old_action->restorer = NULL;
    }
    return result;
}

static void host_mask_from_bionic(uint32_t guest_mask, sigset_t *host_mask)
{
    int index;

    (void)sigemptyset(host_mask);
    for (index = 1; index <= 32; ++index) {
        if ((guest_mask &
             (UINT32_C(1) << (unsigned int)(index - 1))) != 0U) {
            (void)sigaddset(host_mask, index);
        }
    }
}

static uint32_t bionic_mask_from_host(const sigset_t *host_mask)
{
    uint32_t guest_mask = 0U;
    int index;

    for (index = 1; index <= 32; ++index) {
        if (sigismember(host_mask, index) == 1) {
            guest_mask |= UINT32_C(1) << (unsigned int)(index - 1);
        }
    }
    return guest_mask;
}

static int compat_sigprocmask(int how, const uint32_t *guest_set,
                              uint32_t *guest_old_set)
{
    sigset_t host_set;
    sigset_t host_old_set;
    const sigset_t *host_set_pointer = NULL;
    sigset_t *host_old_set_pointer = NULL;
    int result;

    if (guest_set != NULL) {
        host_mask_from_bionic(*guest_set, &host_set);
        host_set_pointer = &host_set;
    }
    if (guest_old_set != NULL) {
        host_old_set_pointer = &host_old_set;
    }
    result = sigprocmask(how, host_set_pointer, host_old_set_pointer);
    if (result == 0 && guest_old_set != NULL) {
        *guest_old_set = bionic_mask_from_host(&host_old_set);
    }
    return result;
}

uint32_t nfsmw_bionic_signal_mask_capture(void)
{
    sigset_t host_mask;

    if (sigprocmask(SIG_SETMASK, NULL, &host_mask) != 0) {
        return 0U;
    }
    return bionic_mask_from_host(&host_mask);
}

void nfsmw_bionic_signal_mask_restore(uint32_t mask)
{
    sigset_t host_mask;

    host_mask_from_bionic(mask, &host_mask);
    (void)sigprocmask(SIG_SETMASK, &host_mask, NULL);
}

void nfsmw_bionic_longjmp_error(void)
{
    (void)fprintf(stderr, "Invalid Bionic ARM jump buffer\n");
    abort();
}

static uint32_t signal_selftest_environment[65]
    __attribute__((aligned(8)));

static void signal_selftest_handler(int signal_number)
{
    (void)signal_number;
    nfsmw_bionic_siglongjmp(signal_selftest_environment, 9);
}

int nfsmw_compat_signal_selftest(void)
{
    const struct bionic_sigaction test_action = {
        signal_selftest_handler, 0U, 0, NULL
    };
    struct bionic_sigaction saved_action;
    uint32_t saved_mask;
    int jump_value;

    if (compat_sigprocmask(SIG_SETMASK, NULL, &saved_mask) != 0 ||
        compat_sigaction(SIGUSR1, &test_action, &saved_action) != 0) {
        return -1;
    }
    jump_value = nfsmw_bionic_sigsetjmp(signal_selftest_environment, 1);
    if (jump_value == 0 && raise(SIGUSR1) != 0) {
        jump_value = -1;
    }
    (void)compat_sigaction(SIGUSR1, &saved_action, NULL);
    (void)compat_sigprocmask(SIG_SETMASK, &saved_mask, NULL);
    return jump_value == 9 ? 0 : -1;
}

void nfsmw_compat_init(void)
{
    size_t index;

    (void)memset(bionic_files, 0, sizeof(bionic_files));
    (void)memset(atexit_entries, 0, sizeof(atexit_entries));
    atexit_count = 0U;
    (void)memset(ctype_storage, 0, sizeof(ctype_storage));
    for (index = 0U; index < 256U; ++index) {
        unsigned char flags = 0U;
        int16_t lower = (int16_t)index;
        if (index >= (size_t)'A' && index <= (size_t)'Z') {
            flags |= 0x01U;
            lower = (int16_t)(index + ((size_t)'a' - (size_t)'A'));
        }
        if (index >= (size_t)'a' && index <= (size_t)'z') {
            flags |= 0x02U;
        }
        if (index >= (size_t)'0' && index <= (size_t)'9') {
            flags |= 0x04U;
        }
        if (index < 32U || index == 127U) {
            flags |= 0x08U;
        }
        if (index >= (size_t)'!' && index <= (size_t)'~' &&
            (flags & 0x07U) == 0U) {
            flags |= 0x10U;
        }
        if (index == (size_t)' ' || (index >= 9U && index <= 13U)) {
            flags |= 0x20U;
        }
        if ((index >= (size_t)'0' && index <= (size_t)'9') ||
            (index >= (size_t)'A' && index <= (size_t)'F') ||
            (index >= (size_t)'a' && index <= (size_t)'f')) {
            flags |= 0x40U;
        }
        if (index == (size_t)' ' || index == (size_t)'\t') {
            flags |= 0x80U;
        }
        ctype_storage[index + 1U] = (char)flags;
        tolower_storage[index + 1U] = lower;
    }
    tolower_storage[0] = -1;
}

void nfsmw_compat_finalize(void)
{
    compat_cxa_finalize(NULL);
}

#define RESOLVE_FUNCTION(symbol, function)                                  \
    do {                                                                    \
        if (strcmp(name, (symbol)) == 0) {                                  \
            return (uintptr_t)&(function);                                  \
        }                                                                   \
    } while (0)

uintptr_t nfsmw_compat_resolve(const char *name)
{
    if (name == NULL) {
        return 0U;
    }
    if (strcmp(name, "__sF") == 0) return (uintptr_t)&bionic_files[0][0];
    if (strcmp(name, "_ctype_") == 0) return (uintptr_t)&ctype_pointer;
    if (strcmp(name, "_tolower_tab_") == 0) return (uintptr_t)&tolower_pointer;
    RESOLVE_FUNCTION("__errno", compat_errno);
    RESOLVE_FUNCTION("abort", compat_abort);
    RESOLVE_FUNCTION("__assert2", compat_assert2);
    RESOLVE_FUNCTION("clock_gettime", compat_clock_gettime);
    RESOLVE_FUNCTION("gettimeofday", compat_gettimeofday);
    RESOLVE_FUNCTION("dlopen", compat_dlopen);
    RESOLVE_FUNCTION("dlsym", compat_dlsym);
    RESOLVE_FUNCTION("dlclose", compat_dlclose);
    RESOLVE_FUNCTION("fclose", compat_fclose);
    RESOLVE_FUNCTION("fdopen", compat_fdopen);
    RESOLVE_FUNCTION("fflush", compat_fflush);
    RESOLVE_FUNCTION("fopen", compat_fopen);
    RESOLVE_FUNCTION("fprintf", compat_fprintf);
    RESOLVE_FUNCTION("fread", compat_fread);
    RESOLVE_FUNCTION("fseek", compat_fseek);
    RESOLVE_FUNCTION("fseeko", compat_fseeko);
    RESOLVE_FUNCTION("ftell", compat_ftell);
    RESOLVE_FUNCTION("ftello", compat_ftello);
    RESOLVE_FUNCTION("fwide", compat_fwide);
    RESOLVE_FUNCTION("fwrite", compat_fwrite);
    RESOLVE_FUNCTION("vfprintf", compat_vfprintf);
    RESOLVE_FUNCTION("stat", compat_stat);
    RESOLVE_FUNCTION("statfs", compat_statfs);
    RESOLVE_FUNCTION("opendir", compat_opendir);
    RESOLVE_FUNCTION("closedir", compat_closedir);
    RESOLVE_FUNCTION("readdir", compat_readdir);
    RESOLVE_FUNCTION("readdir_r", compat_readdir_r);
    RESOLVE_FUNCTION("__android_log_print", compat_android_log_print);
    RESOLVE_FUNCTION("__android_log_write", compat_android_log_write);
    RESOLVE_FUNCTION("__android_log_assert", compat_android_log_assert);
    RESOLVE_FUNCTION("AndroidBitmap_getInfo", compat_android_bitmap_get_info);
    RESOLVE_FUNCTION("AndroidBitmap_lockPixels", compat_android_bitmap_lock);
    RESOLVE_FUNCTION("AndroidBitmap_unlockPixels", compat_android_bitmap_unlock);
    RESOLVE_FUNCTION("__cxa_atexit", compat_cxa_atexit);
    RESOLVE_FUNCTION("__cxa_finalize", compat_cxa_finalize);
    RESOLVE_FUNCTION("__cxa_thread_atexit_impl", compat_cxa_thread_atexit);
    RESOLVE_FUNCTION("pthread_mutexattr_init", compat_mutexattr_init);
    RESOLVE_FUNCTION("pthread_mutexattr_destroy", compat_mutexattr_destroy);
    RESOLVE_FUNCTION("pthread_mutexattr_settype", compat_mutexattr_settype);
    RESOLVE_FUNCTION("pthread_mutexattr_setpshared", compat_mutexattr_setpshared);
    RESOLVE_FUNCTION("pthread_mutex_init", compat_pthread_mutex_init);
    RESOLVE_FUNCTION("pthread_mutex_destroy", compat_pthread_mutex_destroy);
    RESOLVE_FUNCTION("pthread_mutex_lock", compat_pthread_mutex_lock);
    RESOLVE_FUNCTION("pthread_mutex_unlock", compat_pthread_mutex_unlock);
    RESOLVE_FUNCTION("pthread_mutex_trylock", compat_pthread_mutex_trylock);
    RESOLVE_FUNCTION("pthread_cond_init", compat_pthread_cond_init);
    RESOLVE_FUNCTION("pthread_cond_destroy", compat_pthread_cond_destroy);
    RESOLVE_FUNCTION("pthread_cond_signal", compat_pthread_cond_signal);
    RESOLVE_FUNCTION("pthread_cond_broadcast", compat_pthread_cond_broadcast);
    RESOLVE_FUNCTION("pthread_cond_wait", compat_pthread_cond_wait);
    RESOLVE_FUNCTION("pthread_cond_timedwait", compat_pthread_cond_timedwait);
    RESOLVE_FUNCTION("pthread_rwlock_init", compat_pthread_rwlock_init);
    RESOLVE_FUNCTION("pthread_rwlock_destroy", compat_pthread_rwlock_destroy);
    RESOLVE_FUNCTION("pthread_rwlock_rdlock", compat_pthread_rwlock_rdlock);
    RESOLVE_FUNCTION("pthread_rwlock_wrlock", compat_pthread_rwlock_wrlock);
    RESOLVE_FUNCTION("pthread_rwlock_unlock", compat_pthread_rwlock_unlock);
    RESOLVE_FUNCTION("pthread_attr_init", compat_pthread_attr_init);
    RESOLVE_FUNCTION("pthread_attr_destroy", compat_pthread_attr_destroy);
    RESOLVE_FUNCTION("pthread_attr_setstacksize", compat_pthread_attr_setstacksize);
    RESOLVE_FUNCTION("pthread_attr_setstack", compat_pthread_attr_setstack);
    RESOLVE_FUNCTION("pthread_attr_getstack", compat_pthread_attr_getstack);
    RESOLVE_FUNCTION("pthread_attr_setschedparam", compat_pthread_attr_setschedparam);
    RESOLVE_FUNCTION("pthread_attr_setschedpolicy", compat_pthread_attr_setschedpolicy);
    RESOLVE_FUNCTION("pthread_attr_setdetachstate", compat_pthread_attr_setdetachstate);
    RESOLVE_FUNCTION("pthread_create", compat_pthread_create);
    RESOLVE_FUNCTION("pthread_self", compat_pthread_self);
    RESOLVE_FUNCTION("pthread_equal", compat_pthread_equal);
    RESOLVE_FUNCTION("pthread_join", compat_pthread_join);
    RESOLVE_FUNCTION("pthread_detach", compat_pthread_detach);
    RESOLVE_FUNCTION("pthread_exit", compat_pthread_exit);
    RESOLVE_FUNCTION("pthread_setschedparam", compat_pthread_setschedparam);
    RESOLVE_FUNCTION("pthread_getschedparam", compat_pthread_getschedparam);
    RESOLVE_FUNCTION("pthread_getattr_np", compat_pthread_getattr_np);
    RESOLVE_FUNCTION("pthread_once", compat_pthread_once);
    RESOLVE_FUNCTION("pthread_key_create", compat_pthread_key_create);
    RESOLVE_FUNCTION("pthread_key_delete", compat_pthread_key_delete);
    RESOLVE_FUNCTION("pthread_setspecific", compat_pthread_setspecific);
    RESOLVE_FUNCTION("pthread_getspecific", compat_pthread_getspecific);
    RESOLVE_FUNCTION("sem_init", compat_sem_init);
    RESOLVE_FUNCTION("sem_destroy", compat_sem_destroy);
    RESOLVE_FUNCTION("sem_wait", compat_sem_wait);
    RESOLVE_FUNCTION("sem_post", compat_sem_post);
    RESOLVE_FUNCTION("sem_trywait", compat_sem_trywait);
    RESOLVE_FUNCTION("sem_getvalue", compat_sem_getvalue);
    RESOLVE_FUNCTION("sem_timedwait", compat_sem_timedwait);
    RESOLVE_FUNCTION("sigaction", compat_sigaction);
    RESOLVE_FUNCTION("sigprocmask", compat_sigprocmask);
    RESOLVE_FUNCTION("setjmp", nfsmw_bionic_setjmp);
    RESOLVE_FUNCTION("sigsetjmp", nfsmw_bionic_sigsetjmp);
    RESOLVE_FUNCTION("longjmp", nfsmw_bionic_longjmp);
    RESOLVE_FUNCTION("siglongjmp", nfsmw_bionic_siglongjmp);
    return 0U;
}
