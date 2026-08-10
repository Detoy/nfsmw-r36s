#include "opensl_bridge.h"
#include "platform_probe.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t sl_result;
typedef void **sl_interface;

enum { SL_SUCCESS = 0U, SL_PLAYSTATE_STOPPED = 1U };

struct pcm_format {
    uint32_t format_type;
    uint32_t channels;
    uint32_t samples_per_second;
    uint32_t bits_per_sample;
    uint32_t container_size;
    uint32_t channel_mask;
    uint32_t endianness;
};

struct data_source { void *locator; void *format; };

static unsigned char handle_token;
static unsigned char iid_engine_token;
static unsigned char iid_play_token;
static unsigned char iid_queue_token;
static unsigned char iid_config_token;
static unsigned char iid_record_token;
static void *iid_engine = &iid_engine_token;
static void *iid_play = &iid_play_token;
static void *iid_queue = &iid_queue_token;
static void *iid_config = &iid_config_token;
static void *iid_record = &iid_record_token;

static uintptr_t object_vtable[10];
static uintptr_t engine_vtable[20];
static uintptr_t play_vtable[12];
static uintptr_t queue_vtable[4];
static uintptr_t config_vtable[4];
static uintptr_t *engine_object_functions = object_vtable;
static uintptr_t *mix_object_functions = object_vtable;
static uintptr_t *player_object_functions = object_vtable;
static uintptr_t *engine_functions = engine_vtable;
static uintptr_t *play_functions = play_vtable;
static uintptr_t *queue_functions = queue_vtable;
static uintptr_t *config_functions = config_vtable;
static sl_interface engine_object = (sl_interface)&engine_object_functions;
static sl_interface mix_object = (sl_interface)&mix_object_functions;
static sl_interface player_object = (sl_interface)&player_object_functions;
static sl_interface engine_interface = (sl_interface)&engine_functions;
static sl_interface play_interface = (sl_interface)&play_functions;
static sl_interface queue_interface = (sl_interface)&queue_functions;
static sl_interface config_interface = (sl_interface)&config_functions;
static void (*queue_callback)(sl_interface caller, void *context);
static void *queue_context;
static uint32_t queue_buffer_size;
static uint32_t play_state = SL_PLAYSTATE_STOPPED;
static int initialized;
static int callback_active;

static uintptr_t function_value(const void *storage, size_t storage_size)
{
    uintptr_t value = 0U;
    if (storage_size == sizeof(value)) (void)memcpy(&value, storage, sizeof(value));
    return value;
}

#define FN_VALUE(function)                                                   \
    function_value(&(__typeof__(&(function))){ &(function) },                \
                   sizeof(&(function)))

static sl_result sl_ok(sl_interface self, ...)
{
    (void)self;
    return SL_SUCCESS;
}

static void sl_void(sl_interface self, ...)
{
    (void)self;
}

static sl_result object_get_state(sl_interface self, uint32_t *state)
{
    (void)self;
    if (state != NULL) *state = 2U;
    return SL_SUCCESS;
}

static sl_result object_get_interface(sl_interface self, const void *iid,
                                      void *result)
{
    void *value = NULL;
    if (self == engine_object && iid == iid_engine) value = engine_interface;
    if (self == player_object && iid == iid_play) value = play_interface;
    if (self == player_object && iid == iid_queue) value = queue_interface;
    if (self == player_object && iid == iid_config) value = config_interface;
    if (result == NULL || value == NULL) return 12U;
    *(void **)result = value;
    return SL_SUCCESS;
}

static void object_destroy(sl_interface self)
{
    if (self == player_object) {
        queue_callback = NULL;
        queue_context = NULL;
        queue_buffer_size = 0U;
    }
}

static sl_result engine_create_audio_player(sl_interface self,
                                            sl_interface *player,
                                            struct data_source *source,
                                            void *sink,
                                            uint32_t interface_count,
                                            const void *interface_ids,
                                            const uint32_t *required)
{
    const struct pcm_format *format = source != NULL ? source->format : NULL;
    int frequency = 44100;
    int channels = 2;
    (void)self; (void)sink; (void)interface_count;
    (void)interface_ids; (void)required;
    if (format != NULL) {
        if (format->samples_per_second >= 1000U)
            frequency = (int)(format->samples_per_second / 1000U);
        if (format->channels >= 1U && format->channels <= 2U)
            channels = (int)format->channels;
    }
    if (player == NULL ||
        nfsmw_platform_runtime_audio_start(frequency, channels) != 0) return 13U;
    *player = player_object;
    (void)printf("G8-OPENSL CreateAudioPlayer frequency=%d channels=%d\n",
                 frequency, channels);
    return SL_SUCCESS;
}

static sl_result engine_create_output_mix(sl_interface self,
                                          sl_interface *mix,
                                          uint32_t interface_count,
                                          const void *interface_ids,
                                          const uint32_t *required)
{
    (void)self; (void)interface_count; (void)interface_ids; (void)required;
    if (mix == NULL) return 13U;
    *mix = mix_object;
    return SL_SUCCESS;
}

static sl_result play_set_state(sl_interface self, uint32_t state)
{
    (void)self;
    play_state = state;
    return SL_SUCCESS;
}

static sl_result play_get_state(sl_interface self, uint32_t *state)
{
    (void)self;
    if (state != NULL) *state = play_state;
    return SL_SUCCESS;
}

static sl_result queue_enqueue(sl_interface self, const void *buffer,
                               uint32_t size)
{
    (void)self;
    if (nfsmw_platform_runtime_audio_queue(buffer, size) != 0) return 13U;
    queue_buffer_size = size;
    return SL_SUCCESS;
}

static sl_result queue_clear(sl_interface self)
{
    (void)self;
    return SL_SUCCESS;
}

static sl_result queue_get_state(sl_interface self, void *state)
{
    uint32_t *words = state;
    (void)self;
    if (words != NULL) {
        words[0] = nfsmw_platform_runtime_audio_queued() != 0U ? 1U : 0U;
        words[1] = 0U;
    }
    return SL_SUCCESS;
}

static sl_result queue_register_callback(
    sl_interface self, void (*callback)(sl_interface, void *), void *context)
{
    (void)self;
    queue_callback = callback;
    queue_context = context;
    return SL_SUCCESS;
}

static sl_result config_set(sl_interface self, const char *key,
                            const void *value, uint32_t size)
{
    (void)self; (void)key; (void)value; (void)size;
    return SL_SUCCESS;
}

static sl_result sl_create_engine(sl_interface *engine, uint32_t options,
                                  const void *option_array,
                                  uint32_t interface_count,
                                  const void *interface_ids,
                                  const uint32_t *required)
{
    (void)options; (void)option_array; (void)interface_count;
    (void)interface_ids; (void)required;
    if (engine == NULL) return 13U;
    *engine = engine_object;
    (void)printf("G8-OPENSL slCreateEngine\n");
    return SL_SUCCESS;
}

static void initialize(void)
{
    size_t index;
    if (initialized != 0) return;
    for (index = 0U; index < 10U; ++index) object_vtable[index] = FN_VALUE(sl_ok);
    object_vtable[2] = FN_VALUE(object_get_state);
    object_vtable[3] = FN_VALUE(object_get_interface);
    object_vtable[5] = FN_VALUE(sl_void);
    object_vtable[6] = FN_VALUE(object_destroy);
    for (index = 0U; index < 20U; ++index) engine_vtable[index] = FN_VALUE(sl_ok);
    engine_vtable[2] = FN_VALUE(engine_create_audio_player);
    engine_vtable[7] = FN_VALUE(engine_create_output_mix);
    for (index = 0U; index < 12U; ++index) play_vtable[index] = FN_VALUE(sl_ok);
    play_vtable[0] = FN_VALUE(play_set_state);
    play_vtable[1] = FN_VALUE(play_get_state);
    queue_vtable[0] = FN_VALUE(queue_enqueue);
    queue_vtable[1] = FN_VALUE(queue_clear);
    queue_vtable[2] = FN_VALUE(queue_get_state);
    queue_vtable[3] = FN_VALUE(queue_register_callback);
    config_vtable[0] = FN_VALUE(config_set);
    config_vtable[1] = FN_VALUE(sl_ok);
    config_vtable[2] = FN_VALUE(sl_ok);
    config_vtable[3] = FN_VALUE(sl_ok);
    initialized = 1;
}

void *nfsmw_opensl_handle(void)
{
    initialize();
    return &handle_token;
}

int nfsmw_opensl_is_handle(void *handle)
{
    return handle == &handle_token;
}

void *nfsmw_opensl_dlsym(const char *name)
{
    uintptr_t value = 0U;
    if (name == NULL) return NULL;
    if (strcmp(name, "slCreateEngine") == 0) value = FN_VALUE(sl_create_engine);
    else if (strcmp(name, "SL_IID_ENGINE") == 0) return &iid_engine;
    else if (strcmp(name, "SL_IID_PLAY") == 0) return &iid_play;
    else if (strcmp(name, "SL_IID_ANDROIDSIMPLEBUFFERQUEUE") == 0)
        return &iid_queue;
    else if (strcmp(name, "SL_IID_ANDROIDCONFIGURATION") == 0)
        return &iid_config;
    else if (strcmp(name, "SL_IID_RECORD") == 0) return &iid_record;
    return (void *)value;
}

void nfsmw_opensl_pump(void)
{
    if (queue_callback != NULL && queue_buffer_size != 0U &&
        callback_active == 0 &&
        nfsmw_platform_runtime_audio_queued() <= queue_buffer_size) {
        callback_active = 1;
        queue_callback(queue_interface, queue_context);
        callback_active = 0;
    }
}
