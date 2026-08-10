#include "jni_bridge.h"
#include "crash_trace.h"
#include "obb_index.h"
#include "opensl_bridge.h"
#include "platform_probe.h"
#include "softfp_bridge.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

enum {
    JNI_TABLE_SLOTS = 240,
    JVM_TABLE_SLOTS = 8,
    METHOD_CAPACITY = 512,
    FIELD_CAPACITY = 128,
    JNI_OK_VALUE = 0,
    JNI_VERSION_1_2_VALUE = 0x00010002,
    JNI_VERSION_1_4_VALUE = 0x00010004,
    JNI_VERSION_1_6_VALUE = 0x00010006,
    FAKE_MAGIC = 0x4a4e4932,
    FAKE_TEXT_CAPACITY = 4096
};

/*
 * The silent FMOD bridge deliberately leaves some optional Event handles
 * empty.  libapp's event-state helper dereferences its wrapper before testing
 * it, which is harmless on Android only when every FMOD event was created.
 * Run 21 reached this helper with r0 == NULL while unloading the second race.
 *
 * This exact libapp build has executable, zero-filled page padding after its
 * first PT_LOAD.  Install a tiny ARM trampoline there: return the helper's
 * normal inactive state (3) for NULL and otherwise replay the displaced
 * prologue.  Every source and cave word is verified before modifying memory.
 */
int nfsmw_apply_app_patches(const struct elf32_image *app_image,
                            char *error, size_t error_size)
{
    enum {
        EVENT_STATE_HELPER = 0x0050eea8U,
        EVENT_STATE_RESUME = 0x0050eeb4U,
        RX_PADDING_CAVE = 0x00a8d9c0U,
        ARM_BRANCH_LIMIT = 0x02000000U
    };
    static const uint32_t expected_prologue[3] = {
        0xe92d4c10U, 0xe28db008U, 0xe24dd008U
    };
    uint32_t trampoline[7] = {
        0xe3500000U, /* cmp r0, #0 */
        0x03a00003U, /* moveq r0, #3 */
        0x012fff1eU, /* bxeq lr */
        0xe92d4c10U, /* displaced push {r4, sl, fp, lr} */
        0xe28db008U, /* displaced add fp, sp, #8 */
        0xe24dd008U, /* displaced sub sp, sp, #8 */
        0U
    };
    const uintptr_t helper = app_image != NULL ?
        app_image->load_bias + EVENT_STATE_HELPER : 0U;
    const uintptr_t resume = app_image != NULL ?
        app_image->load_bias + EVENT_STATE_RESUME : 0U;
    const uintptr_t cave = app_image != NULL ?
        app_image->load_bias + RX_PADDING_CAVE : 0U;
    uintptr_t helper_page;
    uintptr_t cave_page;
    intptr_t displacement;
    uint32_t entry_branch;
    uint32_t current[3];
    uint32_t cave_words[7];
    size_t index;

    if (app_image == NULL || app_image->mapping == NULL ||
        app_image->page_size == 0U ||
        EVENT_STATE_HELPER + sizeof(current) > app_image->mapping_size ||
        RX_PADDING_CAVE + sizeof(trampoline) > app_image->mapping_size) {
        (void)snprintf(error, error_size, "invalid libapp patch image");
        return -1;
    }
    (void)memcpy(current, (const void *)helper, sizeof(current));
    (void)memcpy(cave_words, (const void *)cave, sizeof(cave_words));
    if (memcmp(current, expected_prologue, sizeof(current)) != 0) {
        (void)snprintf(error, error_size,
                       "unexpected event-state helper at 0x%08x",
                       EVENT_STATE_HELPER);
        return -1;
    }
    for (index = 0U; index < 7U; ++index) {
        if (cave_words[index] != 0U) {
            (void)snprintf(error, error_size,
                           "libapp RX patch cave is not empty");
            return -1;
        }
    }

    displacement = (intptr_t)cave - (intptr_t)helper - 8;
    if ((displacement & 3) != 0 ||
        displacement < -(intptr_t)ARM_BRANCH_LIMIT ||
        displacement >= (intptr_t)ARM_BRANCH_LIMIT) {
        (void)snprintf(error, error_size, "event patch branch is out of range");
        return -1;
    }
    entry_branch = 0xea000000U |
        ((uint32_t)(displacement >> 2) & 0x00ffffffU);
    displacement = (intptr_t)resume - (intptr_t)(cave + 24U) - 8;
    if ((displacement & 3) != 0 ||
        displacement < -(intptr_t)ARM_BRANCH_LIMIT ||
        displacement >= (intptr_t)ARM_BRANCH_LIMIT) {
        (void)snprintf(error, error_size, "event patch return is out of range");
        return -1;
    }
    trampoline[6] = 0xea000000U |
        ((uint32_t)(displacement >> 2) & 0x00ffffffU);

    helper_page = helper & ~((uintptr_t)app_image->page_size - 1U);
    cave_page = cave & ~((uintptr_t)app_image->page_size - 1U);
    if (mprotect((void *)helper_page, app_image->page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0 ||
        mprotect((void *)cave_page, app_image->page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        (void)snprintf(error, error_size, "make app patch pages writable: %s",
                       strerror(errno));
        return -1;
    }
    (void)memcpy((void *)cave, trampoline, sizeof(trampoline));
    (void)memcpy((void *)helper, &entry_branch, sizeof(entry_branch));
    __builtin___clear_cache((char *)helper, (char *)helper + 4U);
    __builtin___clear_cache((char *)cave,
                            (char *)cave + sizeof(trampoline));
    if (mprotect((void *)helper_page, app_image->page_size,
                 PROT_READ | PROT_EXEC) != 0 ||
        mprotect((void *)cave_page, app_image->page_size,
                 PROT_READ | PROT_EXEC) != 0) {
        (void)snprintf(error, error_size, "restore app patch pages: %s",
                       strerror(errno));
        return -1;
    }
    (void)printf("G3-PATCH PASS FMOD event-state NULL guard "
                 "helper=0x%08x cave=0x%08x\n",
                 EVENT_STATE_HELPER, RX_PADDING_CAVE);
    return 0;
}

enum {
    JNI_GET_VERSION = 4, JNI_FIND_CLASS = 6,
    JNI_EXCEPTION_OCCURRED = 15, JNI_EXCEPTION_DESCRIBE = 16,
    JNI_EXCEPTION_CLEAR = 17, JNI_PUSH_LOCAL_FRAME = 19,
    JNI_POP_LOCAL_FRAME = 20, JNI_NEW_GLOBAL_REF = 21,
    JNI_DELETE_GLOBAL_REF = 22, JNI_DELETE_LOCAL_REF = 23,
    JNI_IS_SAME_OBJECT = 24, JNI_NEW_LOCAL_REF = 25,
    JNI_ENSURE_LOCAL_CAPACITY = 26, JNI_ALLOC_OBJECT = 27,
    JNI_NEW_OBJECT = 28, JNI_NEW_OBJECT_V = 29, JNI_NEW_OBJECT_A = 30,
    JNI_GET_OBJECT_CLASS = 31, JNI_IS_INSTANCE_OF = 32,
    JNI_GET_METHOD_ID = 33, JNI_CALL_OBJECT_METHOD = 34,
    JNI_CALL_OBJECT_METHOD_V = 35, JNI_CALL_OBJECT_METHOD_A = 36,
    JNI_CALL_BOOLEAN_METHOD = 37, JNI_CALL_BOOLEAN_METHOD_V = 38,
    JNI_CALL_BOOLEAN_METHOD_A = 39, JNI_CALL_INT_METHOD = 49,
    JNI_CALL_INT_METHOD_V = 50, JNI_CALL_INT_METHOD_A = 51,
    JNI_CALL_LONG_METHOD = 52, JNI_CALL_LONG_METHOD_V = 53,
    JNI_CALL_LONG_METHOD_A = 54, JNI_CALL_FLOAT_METHOD = 55,
    JNI_CALL_FLOAT_METHOD_V = 56, JNI_CALL_FLOAT_METHOD_A = 57,
    JNI_CALL_DOUBLE_METHOD = 58, JNI_CALL_DOUBLE_METHOD_V = 59,
    JNI_CALL_DOUBLE_METHOD_A = 60, JNI_CALL_VOID_METHOD = 61,
    JNI_CALL_VOID_METHOD_V = 62, JNI_CALL_VOID_METHOD_A = 63,
    JNI_GET_FIELD_ID = 94, JNI_GET_OBJECT_FIELD = 95,
    JNI_GET_BOOLEAN_FIELD = 96, JNI_GET_INT_FIELD = 100,
    JNI_GET_LONG_FIELD = 101, JNI_GET_FLOAT_FIELD = 102,
    JNI_GET_DOUBLE_FIELD = 103, JNI_SET_OBJECT_FIELD = 104,
    JNI_SET_BOOLEAN_FIELD = 105, JNI_SET_BYTE_FIELD = 106,
    JNI_SET_CHAR_FIELD = 107, JNI_SET_SHORT_FIELD = 108,
    JNI_SET_INT_FIELD = 109, JNI_SET_LONG_FIELD = 110,
    JNI_SET_FLOAT_FIELD = 111, JNI_SET_DOUBLE_FIELD = 112,
    JNI_GET_STATIC_METHOD_ID = 113,
    JNI_CALL_STATIC_OBJECT_METHOD = 114,
    JNI_CALL_STATIC_OBJECT_METHOD_V = 115,
    JNI_CALL_STATIC_OBJECT_METHOD_A = 116,
    JNI_CALL_STATIC_BOOLEAN_METHOD = 117,
    JNI_CALL_STATIC_BOOLEAN_METHOD_V = 118,
    JNI_CALL_STATIC_BOOLEAN_METHOD_A = 119,
    JNI_CALL_STATIC_INT_METHOD = 129, JNI_CALL_STATIC_INT_METHOD_V = 130,
    JNI_CALL_STATIC_INT_METHOD_A = 131, JNI_CALL_STATIC_LONG_METHOD = 132,
    JNI_CALL_STATIC_LONG_METHOD_V = 133,
    JNI_CALL_STATIC_LONG_METHOD_A = 134,
    JNI_CALL_STATIC_FLOAT_METHOD = 135,
    JNI_CALL_STATIC_FLOAT_METHOD_V = 136,
    JNI_CALL_STATIC_FLOAT_METHOD_A = 137,
    JNI_CALL_STATIC_DOUBLE_METHOD = 138,
    JNI_CALL_STATIC_DOUBLE_METHOD_V = 139,
    JNI_CALL_STATIC_DOUBLE_METHOD_A = 140,
    JNI_CALL_STATIC_VOID_METHOD = 141,
    JNI_CALL_STATIC_VOID_METHOD_V = 142,
    JNI_CALL_STATIC_VOID_METHOD_A = 143, JNI_GET_STATIC_FIELD_ID = 144,
    JNI_GET_STATIC_OBJECT_FIELD = 145, JNI_GET_STATIC_BOOLEAN_FIELD = 146,
    JNI_GET_STATIC_INT_FIELD = 150, JNI_GET_STATIC_LONG_FIELD = 151,
    JNI_GET_STATIC_FLOAT_FIELD = 152, JNI_GET_STATIC_DOUBLE_FIELD = 153,
    JNI_NEW_STRING_UTF = 167, JNI_GET_STRING_UTF_LENGTH = 168,
    JNI_GET_STRING_UTF_CHARS = 169, JNI_RELEASE_STRING_UTF_CHARS = 170,
    JNI_GET_ARRAY_LENGTH = 171, JNI_NEW_OBJECT_ARRAY = 172,
    JNI_GET_OBJECT_ARRAY_ELEMENT = 173, JNI_SET_OBJECT_ARRAY_ELEMENT = 174,
    JNI_NEW_BOOLEAN_ARRAY = 175, JNI_NEW_BYTE_ARRAY = 176,
    JNI_NEW_INT_ARRAY = 179, JNI_NEW_LONG_ARRAY = 180,
    JNI_GET_BYTE_ARRAY_ELEMENTS = 184, JNI_GET_INT_ARRAY_ELEMENTS = 187,
    JNI_GET_LONG_ARRAY_ELEMENTS = 189,
    JNI_RELEASE_BYTE_ARRAY_ELEMENTS = 192,
    JNI_RELEASE_INT_ARRAY_ELEMENTS = 195,
    JNI_RELEASE_LONG_ARRAY_ELEMENTS = 197,
    JNI_GET_BYTE_ARRAY_REGION = 200,
    JNI_GET_INT_ARRAY_REGION = 203, JNI_SET_BYTE_ARRAY_REGION = 208,
    JNI_SET_INT_ARRAY_REGION = 211, JNI_MONITOR_ENTER = 217,
    JNI_MONITOR_EXIT = 218, JNI_GET_JAVA_VM = 219,
    JNI_EXCEPTION_CHECK = 228, JNI_NEW_DIRECT_BYTE_BUFFER = 229,
    JNI_GET_DIRECT_BUFFER_ADDRESS = 230,
    JNI_GET_DIRECT_BUFFER_CAPACITY = 231
};

enum { JVM_GET_ENV = 6 };

enum fake_kind {
    FAKE_GENERIC, FAKE_ACTIVITY, FAKE_CLASS, FAKE_STRING, FAKE_OBJECT_ARRAY,
    FAKE_BYTE_ARRAY, FAKE_INT_ARRAY, FAKE_LONG_ARRAY, FAKE_FILE, FAKE_RUN_LOOP,
    FAKE_DISPLAY_METRICS, FAKE_PAINT, FAKE_FONT_METRICS,
    FAKE_BITMAP_GRAPHICS, FAKE_BITMAP, FAKE_NETWORK_STATUS,
    FAKE_MOGA_KEY_EVENT, FAKE_MOGA_STATE_EVENT, FAKE_MOGA_MOTION_EVENT,
    FAKE_DIRECT_BUFFER
};

struct fake_jni_handle { uintptr_t *functions; };

struct fake_object {
    uint32_t magic;
    enum fake_kind kind;
    char *text;
    size_t length;
    void **elements;
    unsigned char *bytes;
    int *ints;
    int64_t *longs;
    int width;
    int height;
    uint32_t stride;
    float text_size;
};

union fake_jvalue {
    unsigned char boolean_value;
    signed char byte_value;
    unsigned short char_value;
    short short_value;
    int int_value;
    int64_t long_value;
    float float_value;
    double double_value;
    void *object_value;
};

_Static_assert(sizeof(union fake_jvalue) == 8U,
               "JNI jvalue must occupy eight bytes");

struct fake_method {
    char name[96];
    char signature[160];
    int is_static;
};

struct fake_field {
    char name[96];
    char signature[96];
    int is_static;
};

static uintptr_t jni_table[JNI_TABLE_SLOTS];
static uintptr_t jvm_table[JVM_TABLE_SLOTS];
static struct fake_jni_handle jni_handle = { jni_table };
static struct fake_jni_handle jvm_handle = { jvm_table };
static struct fake_method methods[METHOD_CAPACITY];
static struct fake_field fields[FIELD_CAPACITY];
static size_t method_count;
static size_t field_count;
static struct fake_object activity = {
    .magic = FAKE_MAGIC, .kind = FAKE_ACTIVITY
};
static struct fake_object activity_class = {
    .magic = FAKE_MAGIC, .kind = FAKE_CLASS
};
static struct fake_object generic_class = {
    .magic = FAKE_MAGIC, .kind = FAKE_CLASS
};
static struct fake_object run_loop = {
    .magic = FAKE_MAGIC, .kind = FAKE_RUN_LOOP
};
static struct fake_object display_metrics = {
    .magic = FAKE_MAGIC, .kind = FAKE_DISPLAY_METRICS
};
static struct fake_object moga_listener = {
    .magic = FAKE_MAGIC, .kind = FAKE_GENERIC
};
static struct fake_object fmod_audio_device = {
    .magic = FAKE_MAGIC, .kind = FAKE_GENERIC
};

static struct fake_method *as_method(void *value);

static float configured_performance_score(void)
{
    static int initialized;
    /* Score 20 intentionally retains the title's highest visual tier. The
     * R36S sustained playable frame rates with that tier once the failed
     * music retry loop was suppressed. */
    static float score = 20.0F;

    if (initialized == 0) {
        const char *configured = getenv("NFSMW_PERFORMANCE_SCORE");

        if (configured != NULL && configured[0] != '\0') {
            char *end = NULL;
            float parsed = strtof(configured, &end);

            if (end != configured && parsed >= 0.0F && parsed <= 100.0F)
                score = parsed;
        }
        (void)printf("G9-PERFORMANCE requested-score=%.0f\n", (double)score);
        initialized = 1;
    }
    return score;
}

static int silent_audio_override(void)
{
    static int initialized;
    static int enabled = 1;

    if (initialized == 0) {
        const char *configured = getenv("NFSMW_SILENT_AUDIO");

        if (configured != NULL && strcmp(configured, "0") == 0)
            enabled = 0;
        (void)printf("G8-SILENT user-music-override=%d\n", enabled);
        initialized = 1;
    }
    return enabled;
}

static int audio_output_enabled(void)
{
    static int initialized;
    static int enabled;

    if (initialized == 0) {
        const char *configured = getenv("NFSMW_AUDIO_OUTPUT");

        if (configured != NULL && strcmp(configured, "1") == 0)
            enabled = 1;
        (void)printf("G8-AUDIO output-enabled=%d\n", enabled);
        initialized = 1;
    }
    return enabled;
}

static uintptr_t function_pointer_value(const void *storage,
                                        size_t storage_size)
{
    uintptr_t value = 0U;
    if (storage_size == sizeof(value)) {
        (void)memcpy(&value, storage, sizeof(value));
    }
    return value;
}

#define FUNCTION_VALUE(function)                                           \
    function_pointer_value(&(__typeof__(&(function))){ &(function) },       \
                           sizeof(&(function)))

static struct fake_object *as_object(void *value)
{
    struct fake_object *object = value;
    return object != NULL && object->magic == FAKE_MAGIC ? object : NULL;
}

static struct fake_object *new_object(enum fake_kind kind, const char *text,
                                      size_t length)
{
    struct fake_object *object = calloc(1U, sizeof(*object));
    if (object == NULL) {
        abort();
    }
    object->magic = FAKE_MAGIC;
    object->kind = kind;
    object->length = length;
    if (text != NULL) {
        object->text = strdup(text);
        if (object->text == NULL) {
            abort();
        }
    }
    return object;
}

static struct fake_object *new_string_n(const char *text, size_t length)
{
    struct fake_object *object;
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        abort();
    }
    (void)memcpy(copy, text, length);
    copy[length] = '\0';
    object = new_object(FAKE_STRING, copy, length);
    free(copy);
    return object;
}

static struct fake_object *new_string(const char *text)
{
    return new_string_n(text != NULL ? text : "",
                        text != NULL ? strlen(text) : 0U);
}

static struct fake_object *new_paint(float text_size)
{
    struct fake_object *paint = new_object(FAKE_PAINT, NULL, 0U);
    paint->text_size = text_size > 0.0F ? text_size : 16.0F;
    return paint;
}

static struct fake_object *new_bitmap_graphics(int width, int height)
{
    struct fake_object *graphics;
    struct fake_object *bitmap;
    size_t stride;
    size_t byte_count;

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        (void)fprintf(stderr, "G4-BITMAP invalid dimensions=%dx%d\n",
                      width, height);
        width = 1;
        height = 1;
    }
    stride = (size_t)width * 4U;
    byte_count = stride * (size_t)height;
    bitmap = new_object(FAKE_BITMAP, NULL, byte_count);
    bitmap->width = width;
    bitmap->height = height;
    bitmap->stride = (uint32_t)stride;
    bitmap->bytes = calloc(byte_count != 0U ? byte_count : 4U, 1U);
    if (bitmap->bytes == NULL) abort();

    graphics = new_object(FAKE_BITMAP_GRAPHICS, NULL, 1U);
    graphics->width = width;
    graphics->height = height;
    graphics->stride = bitmap->stride;
    graphics->elements = calloc(1U, sizeof(*graphics->elements));
    if (graphics->elements == NULL) abort();
    graphics->elements[0] = bitmap;
    (void)printf("G4-BITMAP create width=%d height=%d stride=%u\n",
                 width, height, bitmap->stride);
    return graphics;
}

static struct fake_object *new_moga_event(enum fake_kind kind, int first,
                                           int second)
{
    struct fake_object *event = new_object(kind, NULL, 3U);
    event->ints = calloc(3U, sizeof(*event->ints));
    if (event->ints == NULL) abort();
    event->ints[0] = first;
    event->ints[1] = second;
    event->ints[2] = 1;
    return event;
}

static struct fake_object *new_moga_motion(const short axes[6])
{
    struct fake_object *event =
        new_object(FAKE_MOGA_MOTION_EVENT, NULL, 6U);
    float *values = calloc(6U, sizeof(*values));
    size_t index;
    if (values == NULL) abort();
    for (index = 0U; index < 6U; ++index) {
        int raw = (int)axes[index];
        if (index < 4U) {
            if (raw > -4096 && raw < 4096) raw = 0;
            values[index] = (float)raw / 32767.0F;
        } else {
            values[index] = ((float)raw + 32768.0F) / 65535.0F;
            if (values[index] < 0.05F) values[index] = 0.0F;
        }
    }
    event->bytes = (unsigned char *)values;
    return event;
}

static const char *string_value(void *value)
{
    struct fake_object *object = as_object(value);
    return object != NULL && object->text != NULL ? object->text : "";
}

static uintptr_t jni_unknown(void *environment, ...)
{
    const uintptr_t caller =
        (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    (void)environment;
    (void)fprintf(stderr,
                  "G4-JNI UNKNOWN table call guest-return=0x%08lx\n",
                  (unsigned long)caller);
    return 0U;
}

static int jvm_attach(void *vm, void **environment, void *arguments)
{
    (void)vm; (void)arguments;
    if (environment == NULL) return -1;
    *environment = &jni_handle;
    return JNI_OK_VALUE;
}

static int jvm_detach(void *vm)
{
    (void)vm;
    return JNI_OK_VALUE;
}

static int jvm_get_env(void *vm, void **environment, int version)
{
    (void)vm;
    (void)printf("G4-JNI GetEnv version=0x%08x\n", (unsigned int)version);
    if (environment == NULL ||
        (version != JNI_VERSION_1_2_VALUE &&
         version != JNI_VERSION_1_4_VALUE &&
         version != JNI_VERSION_1_6_VALUE)) {
        return -1;
    }
    *environment = &jni_handle;
    return JNI_OK_VALUE;
}

static int jni_get_version(void *environment)
{
    (void)environment;
    return JNI_VERSION_1_6_VALUE;
}

static void *jni_find_class(void *environment, const char *name)
{
    (void)environment;
    (void)printf("G4-JNI FindClass name=%s\n", name != NULL ? name : "null");
    return new_object(FAKE_CLASS, name != NULL ? name : "", 0U);
}

static void *jni_no_exception(void *environment)
{
    (void)environment;
    return NULL;
}

static void jni_void_noop(void *environment, ...)
{
    (void)environment;
}

static int jni_int_ok(void *environment, ...)
{
    (void)environment;
    return 0;
}

static void *jni_new_global_ref(void *environment, void *object)
{
    (void)environment;
    return object;
}

static void *jni_pop_local_frame(void *environment, void *object)
{
    (void)environment;
    return object;
}

static int jni_is_same_object(void *environment, void *left, void *right)
{
    (void)environment;
    return left == right;
}

static void *jni_alloc_object(void *environment, void *class_reference)
{
    struct fake_object *class_object = as_object(class_reference);
    (void)environment;
    if (class_object != NULL && class_object->text != NULL &&
        strcmp(class_object->text,
               "com/ea/ironmonkey/BitmapGraphics") == 0) {
        return new_bitmap_graphics(1, 1);
    }
    return new_object(FAKE_GENERIC, NULL, 0U);
}

static void *construct_object(void *class_reference, void *method_value,
                              va_list *arguments)
{
    struct fake_object *class_object = as_object(class_reference);
    struct fake_method *method = as_method(method_value);

    if (class_object != NULL && class_object->text != NULL &&
        strcmp(class_object->text,
               "com/ea/ironmonkey/BitmapGraphics") == 0 &&
        method != NULL && strcmp(method->name, "<init>") == 0 &&
        strcmp(method->signature, "(II)V") == 0) {
        int width = va_arg(*arguments, int);
        int height = va_arg(*arguments, int);
        return new_bitmap_graphics(width, height);
    }
    return new_object(FAKE_GENERIC, NULL, 0U);
}

static void *jni_new_object(void *environment, void *class_reference,
                            void *method_value, ...)
{
    va_list arguments;
    void *result;
    (void)environment;
    va_start(arguments, method_value);
    result = construct_object(class_reference, method_value, &arguments);
    va_end(arguments);
    return result;
}

static void *jni_new_object_v(void *environment, void *class_reference,
                              void *method_value, va_list arguments)
{
    (void)environment;
    return construct_object(class_reference, method_value, &arguments);
}

static void *jni_new_object_a(void *environment, void *class_reference,
                              void *method_value, const void *arguments)
{
    const union fake_jvalue *values = arguments;
    struct fake_object *class_object = as_object(class_reference);
    struct fake_method *method = as_method(method_value);
    (void)environment;
    if (class_object != NULL && class_object->text != NULL &&
        strcmp(class_object->text,
               "com/ea/ironmonkey/BitmapGraphics") == 0 &&
        method != NULL && strcmp(method->name, "<init>") == 0 &&
        strcmp(method->signature, "(II)V") == 0 && values != NULL) {
        return new_bitmap_graphics(values[0].int_value,
                                   values[1].int_value);
    }
    return new_object(FAKE_GENERIC, NULL, 0U);
}

static void *jni_get_object_class(void *environment, void *object)
{
    (void)environment;
    return object == &activity ? &activity_class : &generic_class;
}

static int jni_true(void *environment, ...)
{
    (void)environment;
    return 1;
}

static int jni_get_java_vm(void *environment, void **vm)
{
    (void)environment;
    if (vm == NULL) return -1;
    *vm = &jvm_handle;
    return JNI_OK_VALUE;
}

static void *register_method(const char *name, const char *signature,
                             int is_static)
{
    struct fake_method *method;
    size_t index;

    for (index = 0U; index < method_count; ++index) {
        if (methods[index].is_static == is_static &&
            strcmp(methods[index].name, name != NULL ? name : "") == 0 &&
            strcmp(methods[index].signature,
                   signature != NULL ? signature : "") == 0) {
            return &methods[index];
        }
    }
    if (method_count == METHOD_CAPACITY) {
        (void)fprintf(stderr, "G4-JNI method registry exhausted\n");
        abort();
    }
    method = &methods[method_count++];
    (void)snprintf(method->name, sizeof(method->name), "%s",
                   name != NULL ? name : "");
    (void)snprintf(method->signature, sizeof(method->signature), "%s",
                   signature != NULL ? signature : "");
    method->is_static = is_static;
    (void)printf("G4-JNI %s name=%s signature=%s id=%p\n",
                 is_static != 0 ? "GetStaticMethodID" : "GetMethodID",
                 method->name, method->signature, (void *)method);
    return method;
}

static void *jni_get_method_id(void *environment, void *class_reference,
                               const char *name, const char *signature)
{
    (void)environment;
    (void)class_reference;
    return register_method(name, signature, 0);
}

static void *jni_get_static_method_id(void *environment,
                                      void *class_reference,
                                      const char *name,
                                      const char *signature)
{
    (void)environment;
    (void)class_reference;
    return register_method(name, signature, 1);
}

static struct fake_method *as_method(void *value)
{
    struct fake_method *method = value;
    return method >= methods && method < methods + method_count ? method : NULL;
}

static void *dispatch_object(void *receiver, struct fake_method *method,
                             va_list *arguments)
{
    const char *name = method != NULL ? method->name : "";
    struct fake_object *object = as_object(receiver);

    if (strcmp(name, "createPaintFromFamilyName") == 0 ||
        strcmp(name, "createPaintFromFile") == 0) {
        (void)va_arg(*arguments, void *);
        return new_paint((float)va_arg(*arguments, double));
    }
    if (strcmp(name, "getFontMetricsInt") == 0) {
        struct fake_object *metrics =
            new_object(FAKE_FONT_METRICS, NULL, 0U);
        metrics->text_size = object != NULL && object->text_size > 0.0F ?
                             object->text_size : 16.0F;
        return metrics;
    }
    if (strcmp(name, "getBitmap") == 0 && object != NULL &&
        object->kind == FAKE_BITMAP_GRAPHICS && object->elements != NULL) {
        return object->elements[0];
    }
    if (strcmp(name, "getStatus") == 0)
        return new_object(FAKE_NETWORK_STATUS, NULL, 0U);

    if (strcmp(name, "getObbFullPath") == 0) {
        return new_string(nfsmw_obb_path());
    }
    if (strcmp(name, "GetDefaultLanguage") == 0) return new_string("en");
    if (strcmp(name, "GetDeviceLocale") == 0) return new_string("EN-US");
    if (strcmp(name, "GetDeviceName") == 0) return new_string("R36S");
    if (strcmp(name, "GetApplicationVersion") == 0)
        return new_string("1.3.128");
    if (strcmp(name, "getOsVersion") == 0) return new_string("4.4.4");
    if (strcmp(name, "getPackageName") == 0)
        return new_string("com.ea.games.nfs13_row");
    if (strcmp(name, "getRunLoop") == 0) return &run_loop;
    if (strcmp(name, "getDisplayMetrics") == 0) return &display_metrics;
    if (strcmp(name, "getFilesDir") == 0)
        return new_object(FAKE_FILE, "./files", 0U);
    if (strcmp(name, "getCacheDir") == 0)
        return new_object(FAKE_FILE, "./cache", 0U);
    if (strcmp(name, "getExternalStorageDirectory") == 0)
        return new_object(FAKE_FILE, ".", 0U);
    if (strcmp(name, "getAbsolutePath") == 0) {
        return new_string(string_value(receiver));
    }
    if (strcmp(name, "forEach") == 0 || strcmp(name, "list") == 0) {
        void *argument = va_arg(*arguments, void *);
        const char **children = NULL;
        size_t count = nfsmw_obb_list(string_value(argument), &children);
        struct fake_object *array = new_object(FAKE_OBJECT_ARRAY, NULL, count);
        size_t index;

        array->elements = calloc(count != 0U ? count : 1U,
                                 sizeof(*array->elements));
        if (array->elements == NULL) abort();
        for (index = 0U; index < count; ++index) {
            const char *slash = strchr(children[index], '/');
            size_t length = slash != NULL ?
                (size_t)(slash - children[index]) : strlen(children[index]);
            array->elements[index] = new_string_n(children[index], length);
        }
        return array;
    }
    if (strcmp(name, "getNotchesBoundingRects") == 0) {
        struct fake_object *array = new_object(FAKE_OBJECT_ARRAY, NULL, 0U);
        array->elements = calloc(1U, sizeof(*array->elements));
        if (array->elements == NULL) abort();
        return array;
    }
    if (strcmp(name, "loadClass") == 0) {
        void *class_name = va_arg(*arguments, void *);
        return new_object(FAKE_CLASS, string_value(class_name), 0U);
    }
    return new_object(FAKE_GENERIC, NULL, 0U);
}

static void *jni_call_object_method(void *environment, void *object,
                                    void *method_value, ...)
{
    va_list arguments;
    void *result;
    struct fake_method *method = as_method(method_value);
    (void)environment;
    va_start(arguments, method_value);
    if (method != NULL && strcmp(method->name, "getAbsolutePath") == 0) {
        result = new_string(string_value(object));
    } else {
        result = dispatch_object(object, method, &arguments);
    }
    va_end(arguments);
    return result;
}

static void *jni_call_object_method_v(void *environment, void *object,
                                      void *method_value, va_list arguments)
{
    struct fake_method *method = as_method(method_value);
    (void)environment;
    if (method != NULL && strcmp(method->name, "getAbsolutePath") == 0) {
        return new_string(string_value(object));
    }
    return dispatch_object(object, method, &arguments);
}

static void *jni_call_object_method_a(void *environment, void *object,
                                      void *method_value,
                                      const uintptr_t *arguments)
{
    struct fake_method *method = as_method(method_value);
    (void)environment;
    if (method != NULL && strcmp(method->name, "getAbsolutePath") == 0) {
        return new_string(string_value(object));
    }
    if (method != NULL && strcmp(method->name, "getBitmap") == 0) {
        struct fake_object *graphics = as_object(object);
        return graphics != NULL && graphics->kind == FAKE_BITMAP_GRAPHICS &&
               graphics->elements != NULL ? graphics->elements[0] : NULL;
    }
    if (method != NULL &&
        (strcmp(method->name, "createPaintFromFamilyName") == 0 ||
         strcmp(method->name, "createPaintFromFile") == 0) &&
        arguments != NULL) {
        const union fake_jvalue *values = (const void *)arguments;
        return new_paint(values[1].float_value);
    }
    if (method != NULL && strcmp(method->name, "getObbFullPath") == 0)
        return new_string(nfsmw_obb_path());
    if (method != NULL && strcmp(method->name, "GetDefaultLanguage") == 0)
        return new_string("en");
    if (method != NULL && strcmp(method->name, "GetDeviceLocale") == 0)
        return new_string("EN-US");
    if (method != NULL && strcmp(method->name, "GetDeviceName") == 0)
        return new_string("R36S");
    if (method != NULL && strcmp(method->name, "GetApplicationVersion") == 0)
        return new_string("1.3.128");
    if (method != NULL && strcmp(method->name, "getOsVersion") == 0)
        return new_string("4.4.4");
    if (method != NULL && strcmp(method->name, "getPackageName") == 0)
        return new_string("com.ea.games.nfs13_row");
    if (method != NULL && strcmp(method->name, "getRunLoop") == 0)
        return &run_loop;
    if (method != NULL && strcmp(method->name, "getDisplayMetrics") == 0)
        return &display_metrics;
    if (method != NULL && strcmp(method->name, "getStatus") == 0)
        return new_object(FAKE_NETWORK_STATUS, NULL, 0U);
    if (method != NULL && strcmp(method->name, "getFilesDir") == 0)
        return new_object(FAKE_FILE, "./files", 0U);
    if (method != NULL && strcmp(method->name, "getCacheDir") == 0)
        return new_object(FAKE_FILE, "./cache", 0U);
    if (method != NULL &&
        strcmp(method->name, "getExternalStorageDirectory") == 0)
        return new_object(FAKE_FILE, ".", 0U);
    if (method != NULL &&
        (strcmp(method->name, "forEach") == 0 ||
         strcmp(method->name, "list") == 0) && arguments != NULL) {
        const char **children = NULL;
        size_t count = nfsmw_obb_list(string_value((void *)arguments[0]),
                                      &children);
        struct fake_object *array = new_object(FAKE_OBJECT_ARRAY, NULL, count);
        size_t index;
        array->elements = calloc(count != 0U ? count : 1U,
                                 sizeof(*array->elements));
        if (array->elements == NULL) abort();
        for (index = 0U; index < count; ++index) {
            const char *slash = strchr(children[index], '/');
            size_t length = slash != NULL ?
                (size_t)(slash - children[index]) : strlen(children[index]);
            array->elements[index] = new_string_n(children[index], length);
        }
        return array;
    }
    return new_object(FAKE_GENERIC, NULL, 0U);
}

static int dispatch_boolean(struct fake_method *method)
{
    const char *name = method != NULL ? method->name : "";
    if (strcmp(name, "isAnyMusicPlaying") == 0)
        return silent_audio_override();
    return strcmp(name, "useAssetsFileSystem") == 0 ||
           strcmp(name, "isObbAssets") == 0 ||
           strcmp(name, "init") == 0 ||
           strcmp(name, "bindService") == 0 ||
           strcmp(name, "isNetworkWifi") == 0;
}

static int jni_call_boolean_method(void *environment, void *object,
                                   void *method, ...)
{
    (void)environment; (void)object;
    return dispatch_boolean(as_method(method));
}

static int jni_call_boolean_method_v(void *environment, void *object,
                                     void *method, va_list arguments)
{
    (void)environment; (void)object; (void)arguments;
    return dispatch_boolean(as_method(method));
}

static int jni_call_boolean_method_a(void *environment, void *object,
                                     void *method, const uintptr_t *arguments)
{
    (void)environment; (void)object; (void)arguments;
    return dispatch_boolean(as_method(method));
}

static int dispatch_int_v(void *receiver, struct fake_method *method,
                          va_list *arguments)
{
    const char *name = method != NULL ? method->name : "";
    struct fake_object *object = as_object(receiver);
    if (object != NULL && object->ints != NULL) {
        if (strcmp(name, "getAction") == 0) return object->ints[0];
        if (strcmp(name, "getKeyCode") == 0 &&
            object->kind == FAKE_MOGA_KEY_EVENT) return object->ints[1];
        if (strcmp(name, "getState") == 0 &&
            object->kind == FAKE_MOGA_STATE_EVENT) return object->ints[1];
        if (strcmp(name, "getControllerId") == 0) return object->ints[2];
    }
    if (strcmp(name, "ordinal") == 0 && object != NULL &&
        object->kind == FAKE_NETWORK_STATUS) return 3;
    if (strcmp(name, "getAssetSize") == 0) {
        return (int)nfsmw_obb_asset_size(string_value(
            va_arg(*arguments, void *)));
    }
    if (strcmp(name, "getTotalMemory") == 0) return 768;
    if (strcmp(name, "getPerformanceScore") == 0)
        return (int)configured_performance_score();
    if (strcmp(name, "getWidth") == 0) return 640;
    if (strcmp(name, "getHeight") == 0) return 480;
    if (strcmp(name, "getPointerCount") == 0) return 1;
    return 0;
}

static int jni_call_int_method(void *environment, void *object,
                               void *method_value, ...)
{
    struct fake_method *method = as_method(method_value);
    va_list arguments;
    int result;
    (void)environment; (void)object;
    va_start(arguments, method_value);
    result = dispatch_int_v(object, method, &arguments);
    va_end(arguments);
    return result;
}

static int jni_call_int_method_v(void *environment, void *object,
                                 void *method_value, va_list arguments)
{
    (void)environment; (void)object;
    return dispatch_int_v(object, as_method(method_value), &arguments);
}

static int jni_call_int_method_a(void *environment, void *object,
                                 void *method_value,
                                 const uintptr_t *arguments)
{
    struct fake_method *method = as_method(method_value);
    const char *name = method != NULL ? method->name : "";
    struct fake_object *receiver = as_object(object);
    (void)environment;
    if (receiver != NULL && receiver->ints != NULL) {
        if (strcmp(name, "getAction") == 0) return receiver->ints[0];
        if (strcmp(name, "getKeyCode") == 0 &&
            receiver->kind == FAKE_MOGA_KEY_EVENT) return receiver->ints[1];
        if (strcmp(name, "getState") == 0 &&
            receiver->kind == FAKE_MOGA_STATE_EVENT) return receiver->ints[1];
        if (strcmp(name, "getControllerId") == 0) return receiver->ints[2];
    }
    if (strcmp(name, "ordinal") == 0 && receiver != NULL &&
        receiver->kind == FAKE_NETWORK_STATUS) return 3;
    if (strcmp(name, "getAssetSize") == 0 && arguments != NULL)
        return (int)nfsmw_obb_asset_size(string_value((void *)arguments[0]));
    if (strcmp(name, "getTotalMemory") == 0) return 768;
    if (strcmp(name, "getPerformanceScore") == 0)
        return (int)configured_performance_score();
    if (strcmp(name, "getWidth") == 0) return 640;
    if (strcmp(name, "getHeight") == 0) return 480;
    if (strcmp(name, "getPointerCount") == 0) return 1;
    return 0;
}

static int64_t dispatch_long(struct fake_method *method)
{
    const char *name = method != NULL ? method->name : "";

    if (strcmp(name, "getEventTime") == 0)
        return (int64_t)nfsmw_platform_runtime_ticks();
    return (int64_t)time(NULL) * 1000;
}

static int64_t jni_call_long_method(void *environment, void *object,
                                    void *method, ...)
{
    (void)environment; (void)object;
    return dispatch_long(as_method(method));
}

static int64_t jni_call_long_method_v(void *environment, void *object,
                                      void *method, va_list arguments)
{
    (void)environment; (void)object; (void)arguments;
    return dispatch_long(as_method(method));
}

static int64_t jni_call_long_method_a(void *environment, void *object,
                                      void *method,
                                      const uintptr_t *arguments)
{
    (void)environment; (void)object; (void)arguments;
    return dispatch_long(as_method(method));
}

static float moga_axis_value(const struct fake_object *object, int axis)
{
    const float *values;
    size_t index;
    if (object == NULL || object->kind != FAKE_MOGA_MOTION_EVENT ||
        object->bytes == NULL) return 0.0F;
    values = (const float *)object->bytes;
    if (axis == 0) index = 0U;
    else if (axis == 1) index = 1U;
    else if (axis == 11) index = 2U;
    else if (axis == 14) index = 3U;
    else if (axis == 17) index = 4U;
    else if (axis == 18) index = 5U;
    else return 0.0F;
    return values[index];
}

static float dispatch_float(void *receiver, struct fake_method *method,
                            va_list *arguments)
{
    const char *name = method != NULL ? method->name : "";
    struct fake_object *object = as_object(receiver);
    float text_size = object != NULL && object->text_size > 0.0F ?
                      object->text_size : 16.0F;
    if (object != NULL && object->kind == FAKE_MOGA_MOTION_EVENT) {
        if (strcmp(name, "getX") == 0 || strcmp(name, "getRawX") == 0)
            return moga_axis_value(object, 0);
        if (strcmp(name, "getY") == 0 || strcmp(name, "getRawY") == 0)
            return moga_axis_value(object, 1);
        if (strcmp(name, "getXPrecision") == 0 ||
            strcmp(name, "getYPrecision") == 0) return 1.0F;
    }
    if (strcmp(name, "getAxisValue") == 0 && object != NULL &&
        object->kind == FAKE_MOGA_MOTION_EVENT)
        return moga_axis_value(object, va_arg(*arguments, int));
    if (strcmp(name, "getPerformanceScore") == 0)
        return configured_performance_score();
    if (strcmp(name, "getTextSize") == 0) return text_size;
    if (strcmp(name, "measureText") == 0) {
        const char *text = string_value(va_arg(*arguments, void *));
        return (float)strlen(text) * text_size * (6.0F / 7.0F);
    }
    return 0.0F;
}

static float __attribute__((pcs("aapcs")))
jni_call_float_method(void *environment, void *object, void *method, ...)
{
    va_list arguments;
    float result;
    (void)environment;
    va_start(arguments, method);
    result = dispatch_float(object, as_method(method), &arguments);
    va_end(arguments);
    return result;
}

static float __attribute__((pcs("aapcs")))
jni_call_float_method_v(void *environment, void *object, void *method,
                        va_list arguments)
{
    (void)environment;
    return dispatch_float(object, as_method(method), &arguments);
}

static float __attribute__((pcs("aapcs")))
jni_call_float_method_a(void *environment, void *object, void *method,
                        const uintptr_t *arguments)
{
    struct fake_method *fake_method = as_method(method);
    struct fake_object *fake_object = as_object(object);
    const union fake_jvalue *values = (const void *)arguments;
    const char *name = fake_method != NULL ? fake_method->name : "";
    float text_size = fake_object != NULL && fake_object->text_size > 0.0F ?
                      fake_object->text_size : 16.0F;
    (void)environment;
    if (strcmp(name, "getPerformanceScore") == 0)
        return configured_performance_score();
    if (fake_object != NULL &&
        fake_object->kind == FAKE_MOGA_MOTION_EVENT) {
        if (strcmp(name, "getX") == 0 || strcmp(name, "getRawX") == 0)
            return moga_axis_value(fake_object, 0);
        if (strcmp(name, "getY") == 0 || strcmp(name, "getRawY") == 0)
            return moga_axis_value(fake_object, 1);
        if (strcmp(name, "getXPrecision") == 0 ||
            strcmp(name, "getYPrecision") == 0) return 1.0F;
    }
    if (strcmp(name, "getAxisValue") == 0 && fake_object != NULL &&
        fake_object->kind == FAKE_MOGA_MOTION_EVENT && values != NULL)
        return moga_axis_value(fake_object, values[0].int_value);
    if (strcmp(name, "getTextSize") == 0) return text_size;
    if (strcmp(name, "measureText") == 0 && values != NULL)
        return (float)strlen(string_value(values[0].object_value)) *
               text_size * (6.0F / 7.0F);
    return 0.0F;
}

static double __attribute__((pcs("aapcs")))
jni_call_double_method(void *environment, void *object, void *method, ...)
{
    (void)environment; (void)object; (void)method;
    return 1.0;
}

static double __attribute__((pcs("aapcs")))
jni_call_double_method_v(void *environment, void *object, void *method,
                         va_list arguments)
{
    (void)environment; (void)object; (void)method; (void)arguments;
    return 0.0;
}

static double __attribute__((pcs("aapcs")))
jni_call_double_method_a(void *environment, void *object, void *method,
                         const uintptr_t *arguments)
{
    (void)environment; (void)object; (void)method; (void)arguments;
    return 0.0;
}

static const unsigned char glyphs[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static unsigned char glyph_row(unsigned char character, unsigned int row)
{
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z')
        character = (unsigned char)(character - (unsigned char)'a' +
                                    (unsigned char)'A');
    if (character >= (unsigned char)'A' && character <= (unsigned char)'Z')
        return glyphs[character - (unsigned char)'A'][row];
    if (character >= (unsigned char)'0' && character <= (unsigned char)'9')
        return glyphs[26U + character - (unsigned char)'0'][row];
    if (character == (unsigned char)'-') return row == 3U ? 31U : 0U;
    if (character == (unsigned char)'_') return row == 6U ? 31U : 0U;
    if (character == (unsigned char)'.') return row == 6U ? 4U : 0U;
    if (character == (unsigned char)':')
        return row == 2U || row == 5U ? 4U : 0U;
    if (character == (unsigned char)'/') return (unsigned char)(1U << (row > 4U ? 0U : 4U - row));
    if (character == (unsigned char)' ') return 0U;
    return row == 0U || row == 6U ? 31U : 17U;
}

static void draw_text(struct fake_object *graphics,
                      struct fake_object *paint, const char *text,
                      int origin_x, int baseline_y)
{
    struct fake_object *bitmap;
    int scale;
    int cursor_x = origin_x;
    size_t character_index;

    if (graphics == NULL || graphics->kind != FAKE_BITMAP_GRAPHICS ||
        graphics->elements == NULL || text == NULL) return;
    bitmap = as_object(graphics->elements[0]);
    if (bitmap == NULL || bitmap->kind != FAKE_BITMAP ||
        bitmap->bytes == NULL) return;
    scale = (int)((paint != NULL && paint->text_size > 0.0F ?
                   paint->text_size : 16.0F) / 7.0F);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    for (character_index = 0U; text[character_index] != '\0';
         ++character_index) {
        unsigned int row;
        for (row = 0U; row < 7U; ++row) {
            unsigned char bits = glyph_row((unsigned char)text[character_index],
                                           row);
            unsigned int column;
            for (column = 0U; column < 5U; ++column) {
                int pixel_x;
                int pixel_y;
                int inner_y;
                int inner_x;
                if ((bits & (unsigned char)(1U << (4U - column))) == 0U)
                    continue;
                pixel_x = cursor_x + (int)column * scale;
                pixel_y = baseline_y - 7 * scale + (int)row * scale;
                for (inner_y = 0; inner_y < scale; ++inner_y) {
                    for (inner_x = 0; inner_x < scale; ++inner_x) {
                        int x = pixel_x + inner_x;
                        int y = pixel_y + inner_y;
                        unsigned char *pixel;
                        if (x < 0 || y < 0 || x >= bitmap->width ||
                            y >= bitmap->height) continue;
                        pixel = bitmap->bytes + (size_t)y * bitmap->stride +
                                (size_t)x * 4U;
                        pixel[0] = 255U;
                        pixel[1] = 255U;
                        pixel[2] = 255U;
                        pixel[3] = 255U;
                    }
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

static void dispatch_void(void *receiver, struct fake_method *method,
                          va_list *arguments)
{
    const char *name = method != NULL ? method->name : "";
    struct fake_object *object = as_object(receiver);
    if (strcmp(name, "setTextSize") == 0 && object != NULL) {
        object->text_size = (float)va_arg(*arguments, double);
    } else if (strcmp(name, "clear") == 0 && object != NULL &&
               object->kind == FAKE_BITMAP_GRAPHICS &&
               object->elements != NULL) {
        struct fake_object *bitmap = as_object(object->elements[0]);
        if (bitmap != NULL && bitmap->bytes != NULL)
            (void)memset(bitmap->bytes, 0, bitmap->length);
    } else if (strcmp(name, "drawString") == 0) {
        struct fake_object *paint = as_object(va_arg(*arguments, void *));
        const char *text = string_value(va_arg(*arguments, void *));
        int x = va_arg(*arguments, int);
        int y = va_arg(*arguments, int);
        draw_text(object, paint, text, x, y);
    }
}

static void jni_call_void_method(void *environment, void *object,
                                 void *method, ...)
{
    va_list arguments;
    (void)environment;
    va_start(arguments, method);
    dispatch_void(object, as_method(method), &arguments);
    va_end(arguments);
}

static void jni_call_void_method_v(void *environment, void *object,
                                   void *method, va_list arguments)
{
    (void)environment;
    dispatch_void(object, as_method(method), &arguments);
}

static void jni_call_void_method_a(void *environment, void *object,
                                   void *method, const uintptr_t *arguments)
{
    struct fake_method *fake_method = as_method(method);
    struct fake_object *fake_object = as_object(object);
    const union fake_jvalue *values = (const void *)arguments;
    const char *name = fake_method != NULL ? fake_method->name : "";
    (void)environment;
    if (strcmp(name, "setTextSize") == 0 && fake_object != NULL &&
        values != NULL) {
        fake_object->text_size = values[0].float_value;
    } else if (strcmp(name, "clear") == 0 && fake_object != NULL &&
               fake_object->kind == FAKE_BITMAP_GRAPHICS &&
               fake_object->elements != NULL) {
        struct fake_object *bitmap = as_object(fake_object->elements[0]);
        if (bitmap != NULL && bitmap->bytes != NULL)
            (void)memset(bitmap->bytes, 0, bitmap->length);
    } else if (strcmp(name, "drawString") == 0 && values != NULL) {
        draw_text(fake_object, as_object(values[0].object_value),
                  string_value(values[1].object_value), values[2].int_value,
                  values[3].int_value);
    }
}

static void *register_field(const char *name, const char *signature,
                            int is_static)
{
    struct fake_field *field;
    size_t index;

    for (index = 0U; index < field_count; ++index) {
        if (fields[index].is_static == is_static &&
            strcmp(fields[index].name, name != NULL ? name : "") == 0 &&
            strcmp(fields[index].signature,
                   signature != NULL ? signature : "") == 0) {
            return &fields[index];
        }
    }
    if (field_count == FIELD_CAPACITY) {
        (void)fprintf(stderr, "G4-JNI field registry exhausted\n");
        abort();
    }
    field = &fields[field_count++];
    (void)snprintf(field->name, sizeof(field->name), "%s",
                   name != NULL ? name : "");
    (void)snprintf(field->signature, sizeof(field->signature), "%s",
                   signature != NULL ? signature : "");
    field->is_static = is_static;
    (void)printf("G4-JNI %s name=%s signature=%s\n",
                 is_static != 0 ? "GetStaticFieldID" : "GetFieldID",
                 field->name, field->signature);
    return field;
}

static void *jni_get_field_id(void *environment, void *class_reference,
                              const char *name, const char *signature)
{
    (void)environment; (void)class_reference;
    return register_field(name, signature, 0);
}

static void *jni_get_static_field_id(void *environment, void *class_reference,
                                     const char *name, const char *signature)
{
    (void)environment; (void)class_reference;
    return register_field(name, signature, 1);
}

static struct fake_field *as_field(void *value)
{
    struct fake_field *field = value;
    return field >= fields && field < fields + field_count ? field : NULL;
}

static void *jni_get_object_field(void *environment, void *object, void *field)
{
    (void)environment; (void)object; (void)field;
    return new_object(FAKE_GENERIC, NULL, 0U);
}

static int jni_get_boolean_field(void *environment, void *object, void *field)
{
    (void)environment; (void)object; (void)field;
    return 0;
}

static int jni_get_int_field(void *environment, void *object, void *field_value)
{
    struct fake_field *field = as_field(field_value);
    struct fake_object *fake_object = as_object(object);
    const char *name = field != NULL ? field->name : "";
    float text_size = fake_object != NULL && fake_object->text_size > 0.0F ?
                      fake_object->text_size : 16.0F;
    (void)environment;
    if (strcmp(name, "widthPixels") == 0) return 640;
    if (strcmp(name, "heightPixels") == 0) return 480;
    if (strcmp(name, "densityDpi") == 0) return 160;
    if (strcmp(name, "ascent") == 0) return -(int)(text_size * 0.75F);
    if (strcmp(name, "descent") == 0) return (int)(text_size * 0.25F);
    if (strcmp(name, "bottom") == 0) return (int)(text_size * 0.25F);
    if (strcmp(name, "top") == 0) return -(int)(text_size * 0.875F);
    return 0;
}

int nfsmw_jni_bitmap_info(void *bitmap_value, uint32_t information[5])
{
    struct fake_object *bitmap = as_object(bitmap_value);
    if (bitmap == NULL || bitmap->kind != FAKE_BITMAP ||
        information == NULL) return -1;
    information[0] = (uint32_t)bitmap->width;
    information[1] = (uint32_t)bitmap->height;
    information[2] = bitmap->stride;
    information[3] = 1U;
    information[4] = 0U;
    return 0;
}

int nfsmw_jni_bitmap_lock(void *bitmap_value, void **pixels)
{
    struct fake_object *bitmap = as_object(bitmap_value);
    if (bitmap == NULL || bitmap->kind != FAKE_BITMAP || pixels == NULL ||
        bitmap->bytes == NULL) return -1;
    *pixels = bitmap->bytes;
    return 0;
}

int nfsmw_jni_bitmap_unlock(void *bitmap_value)
{
    struct fake_object *bitmap = as_object(bitmap_value);
    return bitmap != NULL && bitmap->kind == FAKE_BITMAP ? 0 : -1;
}

static int64_t jni_get_long_field(void *environment, void *object, void *field)
{
    (void)environment; (void)object; (void)field;
    return 0;
}

static float __attribute__((pcs("aapcs")))
jni_get_float_field(void *environment, void *object, void *field)
{
    (void)environment; (void)object; (void)field;
    return 1.0F;
}

static double __attribute__((pcs("aapcs")))
jni_get_double_field(void *environment, void *object, void *field)
{
    (void)environment; (void)object; (void)field;
    return 0.0;
}

static void *jni_new_string_utf(void *environment, const char *text)
{
    (void)environment;
    return new_string(text);
}

static int jni_get_string_utf_length(void *environment, void *string)
{
    (void)environment;
    return (int)strlen(string_value(string));
}

static const char *jni_get_string_utf_chars(void *environment, void *string,
                                            unsigned char *is_copy)
{
    (void)environment;
    if (is_copy != NULL) *is_copy = 0U;
    return string_value(string);
}

static int jni_get_array_length(void *environment, void *array_value)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    return array != NULL ? (int)array->length : 0;
}

static void *jni_new_object_array(void *environment, int length,
                                  void *class_reference, void *initial)
{
    struct fake_object *array;
    int index;
    (void)environment; (void)class_reference;
    if (length < 0) length = 0;
    array = new_object(FAKE_OBJECT_ARRAY, NULL, (size_t)length);
    array->elements = calloc(length != 0 ? (size_t)length : 1U,
                             sizeof(*array->elements));
    if (array->elements == NULL) abort();
    for (index = 0; index < length; ++index) array->elements[index] = initial;
    return array;
}

static void *jni_get_object_array_element(void *environment, void *array_value,
                                          int index)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    return array != NULL && index >= 0 && (size_t)index < array->length ?
           array->elements[index] : NULL;
}

static void jni_set_object_array_element(void *environment, void *array_value,
                                         int index, void *value)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    if (array != NULL && index >= 0 && (size_t)index < array->length)
        array->elements[index] = value;
}

static void *jni_new_primitive_array(enum fake_kind kind, int length)
{
    struct fake_object *array;
    if (length < 0) length = 0;
    array = new_object(kind, NULL, (size_t)length);
    if (kind == FAKE_BYTE_ARRAY) {
        array->bytes = calloc(length != 0 ? (size_t)length : 1U, 1U);
        if (array->bytes == NULL) abort();
    } else if (kind == FAKE_LONG_ARRAY) {
        array->longs = calloc(length != 0 ? (size_t)length : 1U,
                              sizeof(*array->longs));
        if (array->longs == NULL) abort();
    } else {
        array->ints = calloc(length != 0 ? (size_t)length : 1U,
                             sizeof(*array->ints));
        if (array->ints == NULL) abort();
    }
    return array;
}

static void *jni_new_byte_array(void *environment, int length)
{ (void)environment; return jni_new_primitive_array(FAKE_BYTE_ARRAY, length); }
static void *jni_new_int_array(void *environment, int length)
{ (void)environment; return jni_new_primitive_array(FAKE_INT_ARRAY, length); }
static void *jni_new_long_array(void *environment, int length)
{ (void)environment; return jni_new_primitive_array(FAKE_LONG_ARRAY, length); }
static void *jni_array_elements(void *environment, void *array_value,
                                unsigned char *is_copy)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    if (is_copy != NULL) *is_copy = 0U;
    if (array == NULL) return NULL;
    if (array->kind == FAKE_BYTE_ARRAY) return array->bytes;
    if (array->kind == FAKE_INT_ARRAY) return array->ints;
    if (array->kind == FAKE_LONG_ARRAY) return array->longs;
    return NULL;
}

static void jni_byte_array_region(void *environment, void *array_value,
                                  int start, int length, unsigned char *buffer)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    if (array != NULL && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length)
        (void)memcpy(buffer, array->bytes + start, (size_t)length);
}

static void jni_int_array_region(void *environment, void *array_value,
                                 int start, int length, int *buffer)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    if (array != NULL && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length)
        (void)memcpy(buffer, array->ints + start,
                     (size_t)length * sizeof(*buffer));
}

static void jni_set_byte_array_region(void *environment, void *array_value,
                                      int start, int length,
                                      const unsigned char *buffer)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    if (array != NULL && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length)
        (void)memcpy(array->bytes + start, buffer, (size_t)length);
}
static void jni_set_int_array_region(void *environment, void *array_value,
                                     int start, int length, const int *buffer)
{
    struct fake_object *array = as_object(array_value);
    (void)environment;
    if (array != NULL && start >= 0 && length >= 0 &&
        (size_t)(start + length) <= array->length)
        (void)memcpy(array->ints + start, buffer,
                     (size_t)length * sizeof(*buffer));
}

static void *jni_new_direct_byte_buffer(void *environment, void *address,
                                        int64_t capacity)
{
    struct fake_object *buffer;
    (void)environment;
    if (address == NULL || capacity < 0) return NULL;
    buffer = new_object(FAKE_DIRECT_BUFFER, NULL, (size_t)capacity);
    buffer->bytes = address;
    return buffer;
}

static void *jni_get_direct_buffer_address(void *environment,
                                           void *buffer_value)
{
    struct fake_object *buffer = as_object(buffer_value);
    (void)environment;
    return buffer != NULL && buffer->kind == FAKE_DIRECT_BUFFER ?
           buffer->bytes : NULL;
}

static int64_t jni_get_direct_buffer_capacity(void *environment,
                                              void *buffer_value)
{
    struct fake_object *buffer = as_object(buffer_value);
    (void)environment;
    return buffer != NULL && buffer->kind == FAKE_DIRECT_BUFFER ?
           (int64_t)buffer->length : -1;
}

static void initialize_tables(void)
{
    size_t index;
    uintptr_t unknown = FUNCTION_VALUE(jni_unknown);
#define JNI_SET(slot, function) jni_table[(slot)] = FUNCTION_VALUE(function)
    for (index = 0U; index < JNI_TABLE_SLOTS; ++index) jni_table[index] = unknown;
    for (index = 0U; index < JVM_TABLE_SLOTS; ++index) jvm_table[index] = unknown;
    JNI_SET(JNI_GET_VERSION, jni_get_version);
    JNI_SET(JNI_FIND_CLASS, jni_find_class);
    JNI_SET(JNI_EXCEPTION_OCCURRED, jni_no_exception);
    JNI_SET(JNI_EXCEPTION_DESCRIBE, jni_void_noop);
    JNI_SET(JNI_EXCEPTION_CLEAR, jni_void_noop);
    JNI_SET(JNI_PUSH_LOCAL_FRAME, jni_int_ok);
    JNI_SET(JNI_POP_LOCAL_FRAME, jni_pop_local_frame);
    JNI_SET(JNI_NEW_GLOBAL_REF, jni_new_global_ref);
    JNI_SET(JNI_DELETE_GLOBAL_REF, jni_void_noop);
    JNI_SET(JNI_DELETE_LOCAL_REF, jni_void_noop);
    JNI_SET(JNI_IS_SAME_OBJECT, jni_is_same_object);
    JNI_SET(JNI_NEW_LOCAL_REF, jni_new_global_ref);
    JNI_SET(JNI_ENSURE_LOCAL_CAPACITY, jni_int_ok);
    JNI_SET(JNI_ALLOC_OBJECT, jni_alloc_object);
    JNI_SET(JNI_NEW_OBJECT, jni_new_object);
    JNI_SET(JNI_NEW_OBJECT_V, jni_new_object_v);
    JNI_SET(JNI_NEW_OBJECT_A, jni_new_object_a);
    JNI_SET(JNI_GET_OBJECT_CLASS, jni_get_object_class);
    JNI_SET(JNI_IS_INSTANCE_OF, jni_true);
    JNI_SET(JNI_GET_METHOD_ID, jni_get_method_id);
    JNI_SET(JNI_CALL_OBJECT_METHOD, jni_call_object_method);
    JNI_SET(JNI_CALL_OBJECT_METHOD_V, jni_call_object_method_v);
    JNI_SET(JNI_CALL_OBJECT_METHOD_A, jni_call_object_method_a);
    JNI_SET(JNI_CALL_BOOLEAN_METHOD, jni_call_boolean_method);
    JNI_SET(JNI_CALL_BOOLEAN_METHOD_V, jni_call_boolean_method_v);
    JNI_SET(JNI_CALL_BOOLEAN_METHOD_A, jni_call_boolean_method_a);
    JNI_SET(JNI_CALL_INT_METHOD, jni_call_int_method);
    JNI_SET(JNI_CALL_INT_METHOD_V, jni_call_int_method_v);
    JNI_SET(JNI_CALL_INT_METHOD_A, jni_call_int_method_a);
    JNI_SET(JNI_CALL_LONG_METHOD, jni_call_long_method);
    JNI_SET(JNI_CALL_LONG_METHOD_V, jni_call_long_method_v);
    JNI_SET(JNI_CALL_LONG_METHOD_A, jni_call_long_method_a);
    JNI_SET(JNI_CALL_FLOAT_METHOD, jni_call_float_method);
    JNI_SET(JNI_CALL_FLOAT_METHOD_V, jni_call_float_method_v);
    JNI_SET(JNI_CALL_FLOAT_METHOD_A, jni_call_float_method_a);
    JNI_SET(JNI_CALL_DOUBLE_METHOD, jni_call_double_method);
    JNI_SET(JNI_CALL_DOUBLE_METHOD_V, jni_call_double_method_v);
    JNI_SET(JNI_CALL_DOUBLE_METHOD_A, jni_call_double_method_a);
    JNI_SET(JNI_CALL_VOID_METHOD, jni_call_void_method);
    JNI_SET(JNI_CALL_VOID_METHOD_V, jni_call_void_method_v);
    JNI_SET(JNI_CALL_VOID_METHOD_A, jni_call_void_method_a);
    JNI_SET(JNI_GET_FIELD_ID, jni_get_field_id);
    JNI_SET(JNI_GET_OBJECT_FIELD, jni_get_object_field);
    JNI_SET(JNI_GET_BOOLEAN_FIELD, jni_get_boolean_field);
    JNI_SET(JNI_GET_INT_FIELD, jni_get_int_field);
    JNI_SET(JNI_GET_LONG_FIELD, jni_get_long_field);
    JNI_SET(JNI_GET_FLOAT_FIELD, jni_get_float_field);
    JNI_SET(JNI_GET_DOUBLE_FIELD, jni_get_double_field);
    JNI_SET(JNI_SET_OBJECT_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_BOOLEAN_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_BYTE_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_CHAR_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_SHORT_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_INT_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_LONG_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_FLOAT_FIELD, jni_void_noop);
    JNI_SET(JNI_SET_DOUBLE_FIELD, jni_void_noop);
    JNI_SET(JNI_GET_STATIC_METHOD_ID, jni_get_static_method_id);
    JNI_SET(JNI_CALL_STATIC_OBJECT_METHOD, jni_call_object_method);
    JNI_SET(JNI_CALL_STATIC_OBJECT_METHOD_V, jni_call_object_method_v);
    JNI_SET(JNI_CALL_STATIC_OBJECT_METHOD_A, jni_call_object_method_a);
    JNI_SET(JNI_CALL_STATIC_BOOLEAN_METHOD, jni_call_boolean_method);
    JNI_SET(JNI_CALL_STATIC_BOOLEAN_METHOD_V, jni_call_boolean_method_v);
    JNI_SET(JNI_CALL_STATIC_BOOLEAN_METHOD_A, jni_call_boolean_method_a);
    JNI_SET(JNI_CALL_STATIC_INT_METHOD, jni_call_int_method);
    JNI_SET(JNI_CALL_STATIC_INT_METHOD_V, jni_call_int_method_v);
    JNI_SET(JNI_CALL_STATIC_INT_METHOD_A, jni_call_int_method_a);
    JNI_SET(JNI_CALL_STATIC_LONG_METHOD, jni_call_long_method);
    JNI_SET(JNI_CALL_STATIC_LONG_METHOD_V, jni_call_long_method_v);
    JNI_SET(JNI_CALL_STATIC_LONG_METHOD_A, jni_call_long_method_a);
    JNI_SET(JNI_CALL_STATIC_FLOAT_METHOD, jni_call_float_method);
    JNI_SET(JNI_CALL_STATIC_FLOAT_METHOD_V, jni_call_float_method_v);
    JNI_SET(JNI_CALL_STATIC_FLOAT_METHOD_A, jni_call_float_method_a);
    JNI_SET(JNI_CALL_STATIC_DOUBLE_METHOD, jni_call_double_method);
    JNI_SET(JNI_CALL_STATIC_DOUBLE_METHOD_V, jni_call_double_method_v);
    JNI_SET(JNI_CALL_STATIC_DOUBLE_METHOD_A, jni_call_double_method_a);
    JNI_SET(JNI_CALL_STATIC_VOID_METHOD, jni_call_void_method);
    JNI_SET(JNI_CALL_STATIC_VOID_METHOD_V, jni_call_void_method_v);
    JNI_SET(JNI_CALL_STATIC_VOID_METHOD_A, jni_call_void_method_a);
    JNI_SET(JNI_GET_STATIC_FIELD_ID, jni_get_static_field_id);
    JNI_SET(JNI_GET_STATIC_OBJECT_FIELD, jni_get_object_field);
    JNI_SET(JNI_GET_STATIC_BOOLEAN_FIELD, jni_get_boolean_field);
    JNI_SET(JNI_GET_STATIC_INT_FIELD, jni_get_int_field);
    JNI_SET(JNI_GET_STATIC_LONG_FIELD, jni_get_long_field);
    JNI_SET(JNI_GET_STATIC_FLOAT_FIELD, jni_get_float_field);
    JNI_SET(JNI_GET_STATIC_DOUBLE_FIELD, jni_get_double_field);
    JNI_SET(JNI_NEW_STRING_UTF, jni_new_string_utf);
    JNI_SET(JNI_GET_STRING_UTF_LENGTH, jni_get_string_utf_length);
    JNI_SET(JNI_GET_STRING_UTF_CHARS, jni_get_string_utf_chars);
    JNI_SET(JNI_RELEASE_STRING_UTF_CHARS, jni_void_noop);
    JNI_SET(JNI_GET_ARRAY_LENGTH, jni_get_array_length);
    JNI_SET(JNI_NEW_OBJECT_ARRAY, jni_new_object_array);
    JNI_SET(JNI_GET_OBJECT_ARRAY_ELEMENT, jni_get_object_array_element);
    JNI_SET(JNI_SET_OBJECT_ARRAY_ELEMENT, jni_set_object_array_element);
    JNI_SET(JNI_NEW_BOOLEAN_ARRAY, jni_new_byte_array);
    JNI_SET(JNI_NEW_BYTE_ARRAY, jni_new_byte_array);
    JNI_SET(JNI_NEW_INT_ARRAY, jni_new_int_array);
    JNI_SET(JNI_NEW_LONG_ARRAY, jni_new_long_array);
    JNI_SET(JNI_GET_BYTE_ARRAY_ELEMENTS, jni_array_elements);
    JNI_SET(JNI_GET_INT_ARRAY_ELEMENTS, jni_array_elements);
    JNI_SET(JNI_GET_LONG_ARRAY_ELEMENTS, jni_array_elements);
    JNI_SET(JNI_RELEASE_BYTE_ARRAY_ELEMENTS, jni_void_noop);
    JNI_SET(JNI_RELEASE_INT_ARRAY_ELEMENTS, jni_void_noop);
    JNI_SET(JNI_RELEASE_LONG_ARRAY_ELEMENTS, jni_void_noop);
    JNI_SET(JNI_GET_BYTE_ARRAY_REGION, jni_byte_array_region);
    JNI_SET(JNI_GET_INT_ARRAY_REGION, jni_int_array_region);
    JNI_SET(JNI_SET_BYTE_ARRAY_REGION, jni_set_byte_array_region);
    JNI_SET(JNI_SET_INT_ARRAY_REGION, jni_set_int_array_region);
    JNI_SET(JNI_MONITOR_ENTER, jni_int_ok);
    JNI_SET(JNI_MONITOR_EXIT, jni_int_ok);
    JNI_SET(JNI_GET_JAVA_VM, jni_get_java_vm);
    JNI_SET(JNI_EXCEPTION_CHECK, jni_int_ok);
    JNI_SET(JNI_NEW_DIRECT_BYTE_BUFFER, jni_new_direct_byte_buffer);
    JNI_SET(JNI_GET_DIRECT_BUFFER_ADDRESS, jni_get_direct_buffer_address);
    JNI_SET(JNI_GET_DIRECT_BUFFER_CAPACITY, jni_get_direct_buffer_capacity);
    jvm_table[4] = FUNCTION_VALUE(jvm_attach);
    jvm_table[5] = FUNCTION_VALUE(jvm_detach);
    jvm_table[JVM_GET_ENV] = FUNCTION_VALUE(jvm_get_env);
    jvm_table[7] = FUNCTION_VALUE(jvm_attach);
    method_count = 0U;
    field_count = 0U;
    (void)memset(methods, 0, sizeof(methods));
    (void)memset(fields, 0, sizeof(fields));
#undef JNI_SET
}

static int bitmap_bridge_selftest(void)
{
    struct fake_object *graphics = new_bitmap_graphics(32, 16);
    struct fake_object *paint = new_paint(7.0F);
    struct fake_object *text = new_string("A1");
    struct fake_object *bitmap;
    void *pixels = NULL;
    uint32_t information[5];
    size_t index;
    int saw_ink = 0;

    draw_text(graphics, paint, string_value(text), 0, 8);
    bitmap = as_object(graphics->elements[0]);
    if (bitmap == NULL) return -1;
    if (nfsmw_jni_bitmap_info(bitmap, information) != 0 ||
        nfsmw_jni_bitmap_lock(bitmap, &pixels) != 0 || pixels == NULL ||
        information[0] != 32U || information[1] != 16U ||
        information[2] != 128U || information[3] != 1U ||
        nfsmw_jni_bitmap_unlock(bitmap) != 0) return -1;
    for (index = 0U; index < bitmap->length; ++index) {
        if (((unsigned char *)pixels)[index] != 0U) {
            saw_ink = 1;
            break;
        }
    }
    if (saw_ink == 0) return -1;
    (void)printf("G4-BITMAP PASS RGBA=32x16 stride=128 fallback-font=1\n");
    return 0;
}

static uintptr_t required_export(const struct elf32_image *image,
                                 const char *name, char *error,
                                 size_t error_size)
{
    uintptr_t address = elf32_find_export(image, name);
    if (address == 0U) {
        (void)snprintf(error, error_size, "missing export %s", name);
    }
    return address;
}

int nfsmw_jni_startup(const struct elf32_image *nimble_image,
                      const struct elf32_image *app_image,
                      char *error, size_t error_size)
{
    typedef int (*jni_on_load_function)(void *, void *);
    typedef void (*native_on_create_function)(void *, void *);
    uintptr_t nimble_on_load_address = required_export(
        nimble_image, "JNI_OnLoad", error, error_size);
    uintptr_t on_load_address = required_export(app_image, "JNI_OnLoad",
                                                error, error_size);
    uintptr_t on_create_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeOnCreate",
        error, error_size);
    jni_on_load_function nimble_on_load = NULL;
    jni_on_load_function on_load = NULL;
    native_on_create_function on_create = NULL;
    const char *obb = getenv("NFSMW_OBB_PATH");
    int version;

    if (nimble_on_load_address == 0U || on_load_address == 0U ||
        on_create_address == 0U ||
        sizeof(nimble_on_load) != sizeof(nimble_on_load_address) ||
        sizeof(on_load) != sizeof(on_load_address) ||
        sizeof(on_create) != sizeof(on_create_address)) return -1;
    if (obb == NULL || obb[0] == '\0')
        obb = "main.1003128.com.ea.games.nfs13_row.obb";
    if (nfsmw_obb_open(obb, error, error_size) != 0) return -1;
    if (nfsmw_platform_runtime_start(640, 480) != 0) {
        (void)snprintf(error, error_size, "persistent GLES startup failed");
        return -1;
    }
    (void)memcpy(&nimble_on_load, &nimble_on_load_address,
                 sizeof(nimble_on_load));
    (void)memcpy(&on_load, &on_load_address, sizeof(on_load));
    (void)memcpy(&on_create, &on_create_address, sizeof(on_create));
    initialize_tables();
    if (bitmap_bridge_selftest() != 0) {
        (void)snprintf(error, error_size, "bitmap bridge self-test failed");
        return -1;
    }
    (void)printf("=== G4 libNimble JNI_OnLoad ===\n");
    version = nimble_on_load(&jvm_handle, NULL);
    (void)printf("G4-JNI leave libNimble JNI_OnLoad version=0x%08x\n",
                 (unsigned int)version);
    if (version != JNI_VERSION_1_6_VALUE) {
        (void)snprintf(error, error_size,
                       "libNimble JNI_OnLoad returned 0x%08x",
                       (unsigned int)version);
        return -1;
    }
    (void)printf("=== G4 libapp JNI_OnLoad ===\n");
    version = on_load(&jvm_handle, NULL);
    (void)printf("G4-JNI leave libapp JNI_OnLoad version=0x%08x\n",
                 (unsigned int)version);
    if (version != JNI_VERSION_1_2_VALUE) {
        (void)snprintf(error, error_size, "JNI_OnLoad returned 0x%08x",
                       (unsigned int)version);
        return -1;
    }
    (void)printf("=== G4 nativeOnCreate ===\n");
    on_create(&jni_handle, &activity);
    (void)printf("G4-JNI PASS JNI_OnLoad/nativeOnCreate methods=%zu\n",
                 method_count);
    return 0;
}

int nfsmw_jni_run(const struct elf32_image *fmod_image,
                  const struct elf32_image *app_image,
                  char *error, size_t error_size)
{
    typedef void (*native_simple_function)(void *, void *);
    typedef void (*native_moga_function)(void *, void *, void *);
    typedef void *(*native_controller_instance_function)(void);
    typedef void (NFSMW_SOFTFP *native_touch_function)(
        void *, void *, int, int, float, float);
    typedef int (*fmod_get_info_function)(void *, void *, int);
    typedef int (*fmod_process_function)(void *, void *, void *);
    uintptr_t surface_created_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeSurfaceCreated",
        error, error_size);
    uintptr_t surface_changed_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeSurfaceChanged",
        error, error_size);
    uintptr_t start_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeOnStart",
        error, error_size);
    uintptr_t resume_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeOnResume",
        error, error_size);
    uintptr_t pause_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeOnPause",
        error, error_size);
    uintptr_t stop_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeOnStop",
        error, error_size);
    uintptr_t destroy_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameActivityMain_nativeOnDestroy",
        error, error_size);
    uintptr_t tick_address = required_export(app_image,
        "Java_com_ea_ironmonkey_RunLoop_nativeOnRunLoopTick",
        error, error_size);
    uintptr_t moga_key_address = required_export(app_image,
        "Java_com_ea_ironmonkey_MogaController_nativeOnKeyEvent",
        error, error_size);
    uintptr_t moga_motion_address = required_export(app_image,
        "Java_com_ea_ironmonkey_MogaController_nativeOnMotionEvent",
        error, error_size);
    uintptr_t moga_state_address = required_export(app_image,
        "Java_com_ea_ironmonkey_MogaController_nativeOnStateEvent",
        error, error_size);
    uintptr_t touch_address = required_export(app_image,
        "Java_com_ea_ironmonkey_GameGLSurfaceView_nativeTouchScreenEvent",
        error, error_size);
    uintptr_t fmod_get_info_address = 0U;
    uintptr_t fmod_process_address = 0U;
    const uintptr_t controller_instance_address = app_image->load_bias +
        0x003f7c88U;
    native_simple_function surface_created = NULL, surface_changed = NULL;
    native_simple_function start = NULL, resume = NULL, pause = NULL;
    native_simple_function stop = NULL, destroy = NULL, tick = NULL;
    native_moga_function moga_key = NULL, moga_motion = NULL;
    native_moga_function moga_state = NULL;
    native_controller_instance_function controller_instance = NULL;
    native_touch_function touch = NULL;
    fmod_get_info_function fmod_get_info = NULL;
    fmod_process_function fmod_process = NULL;
    static const int android_keys[15] = {
        96, 97, 99, 100, 109, 0, 108, 106, 107,
        102, 103, 19, 20, 21, 22
    };
    unsigned char previous[15] = { 0U };
    short previous_axes[6] = { 0, 0, 0, 0, 0, 0 };
    int have_previous_axes = 0;
    int cursor_x = 320;
    int cursor_y = 240;
    int cursor_visible = 0;
    int touch_down = 0;
    unsigned int last_motion_log = 0U;
    unsigned int last_direct_log = 0U;
    int previous_direct_raw = 0;
    unsigned int frame_limit = 18000U;
    unsigned int start_ticks;
    unsigned int frame;
    struct fake_object fmod_buffer = {
        .magic = FAKE_MAGIC, .kind = FAKE_DIRECT_BUFFER
    };
    int fmod_audio_enabled = audio_output_enabled();
    int fmod_audio_started = 0;
    int fmod_mixer_state = -1;
    unsigned int fmod_buffer_size = 0U;
    void *controller_state;
    const char *configured_limit = getenv("NFSMW_TEST_FRAMES");

    if (fmod_audio_enabled != 0) {
        fmod_get_info_address = required_export(fmod_image,
            "Java_org_fmod_FMODAudioDevice_fmodGetInfo", error, error_size);
        fmod_process_address = required_export(fmod_image,
            "Java_org_fmod_FMODAudioDevice_fmodProcess", error, error_size);
    }
    if (surface_created_address == 0U || surface_changed_address == 0U ||
        start_address == 0U || resume_address == 0U ||
        pause_address == 0U || stop_address == 0U ||
        destroy_address == 0U || tick_address == 0U ||
        moga_key_address == 0U || moga_motion_address == 0U ||
        moga_state_address == 0U || touch_address == 0U ||
        (fmod_audio_enabled != 0 &&
         (fmod_get_info_address == 0U || fmod_process_address == 0U)))
        return -1;
    (void)memcpy(&surface_created, &surface_created_address, sizeof(surface_created));
    (void)memcpy(&surface_changed, &surface_changed_address, sizeof(surface_changed));
    (void)memcpy(&start, &start_address, sizeof(start));
    (void)memcpy(&resume, &resume_address, sizeof(resume));
    (void)memcpy(&pause, &pause_address, sizeof(pause));
    (void)memcpy(&stop, &stop_address, sizeof(stop));
    (void)memcpy(&destroy, &destroy_address, sizeof(destroy));
    (void)memcpy(&tick, &tick_address, sizeof(tick));
    (void)memcpy(&moga_key, &moga_key_address, sizeof(moga_key));
    (void)memcpy(&moga_motion, &moga_motion_address, sizeof(moga_motion));
    (void)memcpy(&moga_state, &moga_state_address, sizeof(moga_state));
    (void)memcpy(&controller_instance, &controller_instance_address,
                 sizeof(controller_instance));
    (void)memcpy(&touch, &touch_address, sizeof(touch));
    if (fmod_audio_enabled != 0) {
        (void)memcpy(&fmod_get_info, &fmod_get_info_address,
                     sizeof(fmod_get_info));
        (void)memcpy(&fmod_process, &fmod_process_address,
                     sizeof(fmod_process));
        (void)printf("G8-AUDIOTRACK native Java mixer replacement enabled\n");
    }
    if (configured_limit != NULL && configured_limit[0] != '\0') {
        unsigned long parsed = strtoul(configured_limit, NULL, 10);
        if (parsed <= 360000UL) frame_limit = (unsigned int)parsed;
    }
    if (nfsmw_crash_trace_install() != 0)
        (void)fprintf(stderr, "G-CRASH warning: handler installation failed\n");
    (void)printf("=== G5 GAME SURFACE ===\n");
    start(&jni_handle, &activity);
    resume(&jni_handle, &activity);
    surface_created(&jni_handle, &activity);
    surface_changed(&jni_handle, &activity);
    (void)printf("G5-SURFACE PASS lifecycle returned\n");
    {
        static const int state_values[][2] = {
            { 1, 3 }, { 1, 4 }, { 1, 1 }
        };
        size_t index;
        for (index = 0U;
             index < sizeof(state_values) / sizeof(state_values[0]); ++index) {
            struct fake_object *event = new_moga_event(
                FAKE_MOGA_STATE_EVENT, state_values[index][0],
                state_values[index][1]);
            moga_state(&jni_handle, &moga_listener, event);
            free(event->ints);
            free(event);
        }
        (void)printf("G6-MOGA connected product=MOGA-Pro\n");
    }
    (void)printf("=== G6/G7 GAME LOOP (Back+Start exits) ===\n");
    (void)printf("G6-MOGA steering=direct-guest-state deadzone=4096 "
                 "jni-payload=-1/0/+1\n");
    (void)printf("G6-TOUCH fallback-toggle=Select\n");
    (void)printf("G6-MOGA D-pad=native-key-only analog=independent-X/Y\n");
    start_ticks = nfsmw_platform_runtime_ticks();
    controller_state = controller_instance();
    if (controller_state == NULL) {
        (void)snprintf(error, error_size,
                       "MOGA controller singleton is unavailable");
        return -1;
    }
    for (frame = 0U; frame_limit == 0U || frame < frame_limit; ++frame) {
        short axes[6];
        unsigned char buttons[15];
        size_t index;
        int quit = nfsmw_platform_runtime_input(axes, buttons);
        int axes_changed = have_previous_axes == 0;
        const int raw_steering = (int)axes[0];
        const int raw_vertical = (int)axes[1];
        float direct_steering = 0.0F;
        float direct_vertical = 0.0F;

        /* This mobile control scheme auto-accelerates and uses only X for
         * steering. Brake/reverse remains on L1 as the title expects, while
         * the Y field is retained for two-dimensional menu navigation.
         *
         * Hardware run 20 proved that the game's fractional MOGA steering
         * path corrupts its track/world position, while the exact -1/0/+1
         * values synthesized by the D-pad remain stable. Run 21 completed two
         * races with those values, while run 22 showed that pulse-density
         * steering is perceptible at light input.
         *
         * Keep the JNI MotionEvent on its proven-safe values, then write the
         * normalized float directly to the exact controller field that
         * nativeOnMotionEvent updates. This bypasses the suspect fractional
         * JNI return path while giving game code a smooth IEEE-754 value. */
        {
            int magnitude = raw_steering < 0 ? -raw_steering : raw_steering;

            if (magnitude <= 4096) {
                axes[0] = 0;
            } else {
                if (magnitude > 32767) magnitude = 32767;
                direct_steering = (float)(magnitude - 4096) /
                                  (float)(32767 - 4096);
                if (raw_steering < 0) direct_steering = -direct_steering;
                axes[0] = 0;
            }
        }
        axes[1] = 0;
        {
            int magnitude = raw_vertical < 0 ? -raw_vertical : raw_vertical;

            if (magnitude > 4096) {
                if (magnitude > 32767) magnitude = 32767;
                direct_vertical = (float)(magnitude - 4096) /
                                  (float)(32767 - 4096);
                if (raw_vertical < 0) direct_vertical = -direct_vertical;
            }
        }

        /* AXIS_X/AXIS_Y are the physical stick. AXIS_Z/AXIS_RZ synthesize
         * global menu-tab commands in this title, so leave those neutral.
         *
         * A real MOGA Pro reports its D-pad only as key events. The old mapper
         * also mirrored every D-pad press to full-scale X/Y, so the UI received
         * two navigation commands through different paths. Run 28 exposed the
         * result: its highlight moved while Accept still targeted the previous
         * class/modification widget. Keep D-pad input on the native key path
         * below and reserve X/Y state exclusively for the physical stick. */
        axes[2] = 0;
        axes[3] = 0;
        if (cursor_visible != 0) {
            int cursor_dx = raw_steering / 4096;
            int cursor_dy = raw_vertical / 4096;
            if (buttons[13] != 0U) cursor_dx = -8;
            else if (buttons[14] != 0U) cursor_dx = 8;
            if (buttons[11] != 0U) cursor_dy = -8;
            else if (buttons[12] != 0U) cursor_dy = 8;
            cursor_x += cursor_dx;
            cursor_y += cursor_dy;
            if (cursor_x < 8) cursor_x = 8;
            if (cursor_x > 631) cursor_x = 631;
            if (cursor_y < 8) cursor_y = 8;
            if (cursor_y > 471) cursor_y = 471;
            direct_steering = 0.0F;
            direct_vertical = 0.0F;
        }

        for (index = 0U; index < 6U; ++index) {
            int difference = (int)axes[index] - (int)previous_axes[index];
            if (difference > 512 || difference < -512) axes_changed = 1;
            previous_axes[index] = axes[index];
        }
        if (axes_changed != 0) {
            struct fake_object *event = new_moga_motion(axes);
            moga_motion(&jni_handle, &moga_listener, event);
            free(event->bytes);
            free(event);
            have_previous_axes = 1;
            if (frame < 10U || frame % 300U == 0U ||
                frame - last_motion_log >= 15U) {
                (void)printf("G6-MOGA motion frame=%u axes=%d,%d,%d,%d,%d,%d\n",
                             frame, (int)axes[0], (int)axes[1],
                             (int)axes[2], (int)axes[3], (int)axes[4],
                             (int)axes[5]);
                last_motion_log = frame;
            }
        }
        *(volatile float *)((unsigned char *)controller_state + 0x138U) =
            direct_steering;
        *(volatile float *)((unsigned char *)controller_state + 0x13cU) =
            direct_vertical;
        if (((raw_steering - previous_direct_raw > 512) ||
             (raw_steering - previous_direct_raw < -512)) &&
            (frame < 10U || frame - last_direct_log >= 15U)) {
            uint32_t steering_bits = 0U;
            (void)memcpy(&steering_bits, &direct_steering,
                         sizeof(steering_bits));
            (void)printf("G6-DIRECT stick frame=%u raw=%d,%d value=%.4f,%.4f "
                         "x_bits=0x%08x\n", frame, raw_steering,
                         raw_vertical, (double)direct_steering,
                         (double)direct_vertical, steering_bits);
            last_direct_log = frame;
        }
        previous_direct_raw = raw_steering;

        for (index = 0U; index < 15U; ++index) {
            if (android_keys[index] != 0 && buttons[index] != previous[index]) {
                int send_moga_key;

                if (buttons[index] != 0U && index == 4U &&
                    buttons[6] == 0U) {
                    cursor_visible = cursor_visible == 0;
                    if (cursor_visible == 0 && touch_down != 0) {
                        touch(&jni_handle, &activity, 1, 0,
                              (float)cursor_x, (float)cursor_y);
                        touch_down = 0;
                    }
                    (void)printf("G6-TOUCH cursor=%s frame=%u position=%d,%d\n",
                                 cursor_visible != 0 ? "on" : "off", frame,
                                 cursor_x, cursor_y);
                }
                send_moga_key = index != 4U &&
                    !(cursor_visible != 0 &&
                      (index == 0U || index >= 11U));
                if (send_moga_key != 0) {
                    struct fake_object *event = new_moga_event(
                        FAKE_MOGA_KEY_EVENT, buttons[index] != 0U ? 0 : 1,
                        android_keys[index]);
                    moga_key(&jni_handle, &moga_listener, event);
                    free(event->ints);
                    free(event);
                    (void)printf("G6-MOGA key frame=%u code=%d state=%s\n",
                                 frame, android_keys[index],
                                 buttons[index] != 0U ? "down" : "up");
                } else {
                    (void)printf("G6-MOGA key-suppressed frame=%u code=%d "
                                 "state=%s cursor=%d\n",
                                 frame, android_keys[index],
                                 buttons[index] != 0U ? "down" : "up",
                                 cursor_visible);
                }
                if (buttons[index] != 0U && cursor_visible != 0 &&
                    (index == 9U || index == 10U)) {
                    if (touch_down != 0) {
                        touch(&jni_handle, &activity, 1, 0,
                              (float)cursor_x, (float)cursor_y);
                        touch_down = 0;
                    }
                    cursor_visible = 0;
                    (void)printf("G6-TOUCH cursor=auto-off frame=%u reason=shoulder\n",
                                 frame);
                }
                if (buttons[index] != 0U && cursor_visible != 0 &&
                    index == 0U && touch_down == 0) {
                    touch(&jni_handle, &activity, 0, 0,
                          (float)cursor_x, (float)cursor_y);
                    touch_down = 1;
                    (void)printf("G6-TOUCH down frame=%u position=%d,%d\n",
                                 frame, cursor_x, cursor_y);
                }
                if (index == 0U && buttons[index] == 0U && touch_down != 0) {
                    touch(&jni_handle, &activity, 1, 0,
                          (float)cursor_x, (float)cursor_y);
                    touch_down = 0;
                    (void)printf("G6-TOUCH up frame=%u position=%d,%d\n",
                                 frame, cursor_x, cursor_y);
                }
            }
            previous[index] = buttons[index];
        }
        if (frame < 10U || frame % 300U == 0U)
            (void)printf("G7-FRAME enter=%u\n", frame);
        tick(&jni_handle, &run_loop);
        /*
         * FMOD Ex on this APK uses its Java AudioTrack backend. Android's
         * GameActivityMain starts org.fmod.FMODAudioDevice after onResume;
         * that Java thread allocates a direct ByteBuffer, calls fmodProcess,
         * and writes the resulting signed 16-bit stereo PCM to AudioTrack.
         *
         * This loader intentionally enters the native activity without a VM,
         * so reproduce that small pull loop here and feed the already proven
         * SDL/ALSA queue. The original FMOD mixer, event banks, 3D audio, and
         * all game-side sound logic remain untouched.
         */
        if (fmod_audio_enabled != 0) {
            int sample_rate = fmod_get_info(
                &jni_handle, &fmod_audio_device, 0);

            if (fmod_audio_started == 0 && sample_rate > 0) {
                int dsp_length = fmod_get_info(
                    &jni_handle, &fmod_audio_device, 1);
                int dsp_buffers = fmod_get_info(
                    &jni_handle, &fmod_audio_device, 2);
                uint64_t requested = dsp_length > 0 ?
                    (uint64_t)(unsigned int)dsp_length * 4U : 0U;

                if (sample_rate > 192000 || dsp_length < 64 ||
                    dsp_length > 16384 || dsp_buffers < 1 ||
                    dsp_buffers > 32 || requested > UINT32_MAX) {
                    (void)snprintf(error, error_size,
                                   "invalid FMOD AudioTrack format "
                                   "rate=%d length=%d buffers=%d",
                                   sample_rate, dsp_length, dsp_buffers);
                    return -1;
                }
                fmod_buffer_size = (unsigned int)requested;
                fmod_buffer.bytes = calloc(fmod_buffer_size, 1U);
                if (fmod_buffer.bytes == NULL ||
                    nfsmw_platform_runtime_audio_start(sample_rate, 2) != 0) {
                    (void)snprintf(error, error_size,
                                   "FMOD AudioTrack SDL output startup failed");
                    return -1;
                }
                fmod_buffer.length = fmod_buffer_size;
                fmod_audio_started = 1;
                (void)printf("G8-AUDIOTRACK PASS rate=%dHz "
                             "dsp-frames=%d buffers=%d pcm-bytes=%u\n",
                             sample_rate, dsp_length, dsp_buffers,
                             fmod_buffer_size);
            }
            if (fmod_audio_started != 0) {
                int mixer_running = fmod_get_info(
                    &jni_handle, &fmod_audio_device, 3);

                if (mixer_running != fmod_mixer_state) {
                    (void)printf("G8-AUDIOTRACK mixer-running=%d frame=%u\n",
                                 mixer_running, frame);
                    fmod_mixer_state = mixer_running;
                }
                if (mixer_running == 1 &&
                    nfsmw_platform_runtime_audio_queued() <=
                        fmod_buffer_size * 2U) {
                    int process_result = fmod_process(
                        &jni_handle, &fmod_audio_device, &fmod_buffer);

                    if (process_result != 0 && frame < 60U)
                        (void)printf("G8-AUDIOTRACK process-result=%d "
                                     "frame=%u\n", process_result, frame);
                    if (nfsmw_platform_runtime_audio_queue(
                            fmod_buffer.bytes, fmod_buffer_size) != 0) {
                        (void)snprintf(error, error_size,
                                       "FMOD AudioTrack PCM queue failed");
                        return -1;
                    }
                }
            }
        }
        nfsmw_opensl_pump();
        nfsmw_platform_runtime_present(frame, cursor_x, cursor_y,
                                       cursor_visible);
        if (frame < 10U || frame % 300U == 0U)
            (void)printf("G7-FRAME leave=%u\n", frame);
        if (quit != 0) {
            (void)printf("G6-INPUT exit chord received frame=%u\n", frame);
            ++frame;
            break;
        }
    }
    {
        unsigned int elapsed = nfsmw_platform_runtime_ticks() - start_ticks;
        double fps = elapsed != 0U ? (double)frame * 1000.0 / (double)elapsed : 0.0;
        (void)printf("G7-LOOP PASS frames=%u elapsed_ms=%u fps=%.2f\n",
                     frame, elapsed, fps);
    }
    /*
     * The old Android FMOD layer leaves failed event instances in the race
     * sound graph. Its normal nativeOnDestroy path dereferences one during
     * teardown after gameplay. The process owns all guest mappings, so a
     * compatibility-port exit can safely let the OS reclaim them instead.
     */
    (void)pause;
    (void)stop;
    (void)destroy;
    (void)printf("G7-LOOP PASS teardown=process-exit (guest destroy skipped)\n");
    return 0;
}

void nfsmw_jni_shutdown(void)
{
    nfsmw_platform_runtime_stop();
    nfsmw_obb_close();
}
