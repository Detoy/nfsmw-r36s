#include "platform_probe.h"
#include "softfp_bridge.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct nfsmw_sdl_window sdl_window;
typedef struct nfsmw_sdl_controller sdl_controller;
typedef struct nfsmw_sdl_joystick sdl_joystick;
typedef void *sdl_gl_context;
typedef uint32_t sdl_audio_device;

struct sdl_audio_spec {
    int frequency;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint16_t padding;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int length);
    void *userdata;
};

struct sdl_api {
    void *library;
    int (*init)(uint32_t flags);
    int (*init_subsystem)(uint32_t flags);
    void (*quit)(void);
    const char *(*get_error)(void);
    int (*set_hint)(const char *name, const char *value);
    const char *(*current_video_driver)(void);
    int (*gl_set_attribute)(int attribute, int value);
    sdl_window *(*create_window)(const char *title, int x, int y,
                                 int width, int height, uint32_t flags);
    void (*destroy_window)(sdl_window *window);
    sdl_gl_context (*gl_create_context)(sdl_window *window);
    void (*gl_delete_context)(sdl_gl_context context);
    int (*gl_set_swap_interval)(int interval);
    void (*gl_swap_window)(sdl_window *window);
    void *(*gl_get_proc_address)(const char *name);
    void (*gl_get_drawable_size)(sdl_window *window, int *width, int *height);
    int (*num_joysticks)(void);
    int (*is_game_controller)(int index);
    sdl_controller *(*game_controller_open)(int index);
    const char *(*game_controller_name)(sdl_controller *controller);
    sdl_joystick *(*game_controller_get_joystick)(sdl_controller *controller);
    void (*game_controller_close)(sdl_controller *controller);
    int16_t (*game_controller_get_axis)(sdl_controller *controller,
                                        int axis);
    uint8_t (*game_controller_get_button)(sdl_controller *controller,
                                          int button);
    int (*joystick_num_buttons)(sdl_joystick *joystick);
    uint8_t (*joystick_get_button)(sdl_joystick *joystick, int button);
    void (*pump_events)(void);
    const char *(*current_audio_driver)(void);
    sdl_audio_device (*open_audio_device)(const char *device, int capture,
                                          const struct sdl_audio_spec *wanted,
                                          struct sdl_audio_spec *obtained,
                                          int allowed_changes);
    int (*queue_audio)(sdl_audio_device device, const void *data,
                       uint32_t length);
    uint32_t (*queued_audio_size)(sdl_audio_device device);
    void (*pause_audio_device)(sdl_audio_device device, int pause);
    void (*close_audio_device)(sdl_audio_device device);
    void (*delay)(uint32_t milliseconds);
    uint32_t (*get_ticks)(void);
};

static struct sdl_api *softfp_binding_api;

static void *resolve_softfp_gles(const char *name)
{
    return softfp_binding_api != NULL ?
        softfp_binding_api->gl_get_proc_address(name) : NULL;
}

enum {
    SDL_INIT_AUDIO_VALUE = 0x00000010U,
    SDL_INIT_VIDEO_VALUE = 0x00000020U,
    SDL_INIT_GAMECONTROLLER_VALUE = 0x00002000U,
    SDL_WINDOW_FULLSCREEN_VALUE = 0x00000001U,
    SDL_WINDOW_OPENGL_VALUE = 0x00000002U,
    SDL_WINDOW_SHOWN_VALUE = 0x00000004U,
    SDL_WINDOWPOS_UNDEFINED_VALUE = 0x1FFF0000U,
    SDL_GL_RED_SIZE_VALUE = 0,
    SDL_GL_GREEN_SIZE_VALUE = 1,
    SDL_GL_BLUE_SIZE_VALUE = 2,
    SDL_GL_ALPHA_SIZE_VALUE = 3,
    SDL_GL_DOUBLEBUFFER_VALUE = 5,
    SDL_GL_DEPTH_SIZE_VALUE = 6,
    SDL_GL_STENCIL_SIZE_VALUE = 7,
    SDL_GL_CONTEXT_MAJOR_VERSION_VALUE = 17,
    SDL_GL_CONTEXT_MINOR_VERSION_VALUE = 18,
    SDL_GL_CONTEXT_PROFILE_MASK_VALUE = 21,
    SDL_GL_CONTEXT_PROFILE_ES_VALUE = 0x0004,
    SDL_AUDIO_S16LSB_VALUE = 0x8010,
    GL_VENDOR_VALUE = 0x1F00,
    GL_RENDERER_VALUE = 0x1F01,
    GL_VERSION_VALUE = 0x1F02,
    GL_COLOR_BUFFER_BIT_VALUE = 0x00004000,
    GL_SCISSOR_TEST_VALUE = 0x0C11,
    GL_SCISSOR_BOX_VALUE = 0x0C10,
    GL_COLOR_CLEAR_VALUE = 0x0C22,
    GL_COLOR_WRITEMASK_VALUE = 0x0C23,
    GL_FRAMEBUFFER_VALUE = 0x8D40,
    GL_FRAMEBUFFER_BINDING_VALUE = 0x8CA6,
    GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5,
    GL_RGBA_VALUE = 0x1908,
    GL_UNSIGNED_BYTE_VALUE = 0x1401
};

typedef const unsigned char *(*gl_get_string_fn)(unsigned int name);
typedef void (*gl_clear_color_fn)(float red, float green, float blue,
                                  float alpha);
typedef void (*gl_clear_fn)(unsigned int mask);
typedef unsigned int (*gl_get_error_fn)(void);
typedef void (*gl_get_integer_v_fn)(unsigned int name, int *values);
typedef void (*gl_get_float_v_fn)(unsigned int name, float *values);
typedef void (*gl_get_boolean_v_fn)(unsigned int name, uint8_t *values);
typedef uint8_t (*gl_is_enabled_fn)(unsigned int capability);
typedef void (*gl_enable_fn)(unsigned int capability);
typedef void (*gl_disable_fn)(unsigned int capability);
typedef void (*gl_scissor_fn)(int x, int y, int width, int height);
typedef void (*gl_color_mask_fn)(uint8_t red, uint8_t green, uint8_t blue,
                                 uint8_t alpha);
typedef void (*gl_bind_framebuffer_fn)(unsigned int target,
                                       unsigned int framebuffer);
typedef unsigned int (*gl_check_framebuffer_status_fn)(unsigned int target);
typedef void (*gl_read_pixels_fn)(int x, int y, int width, int height,
                                  unsigned int format,
                                  unsigned int type, void *pixels);

static const char *sdl_error(const struct sdl_api *api)
{
    const char *message = api->get_error != NULL ? api->get_error() : NULL;

    return message != NULL && message[0] != '\0' ? message : "unknown error";
}

static int load_symbol(void *library, const char *name,
                       void *destination, size_t destination_size)
{
    void *address = dlsym(library, name);

    if (address == NULL || destination_size != sizeof(address)) {
        return -1;
    }
    (void)memcpy(destination, &address, sizeof(address));
    return 0;
}

#define LOAD_API(api, member, name)                                         \
    do {                                                                    \
        if (load_symbol((api)->library, (name), &(api)->member,              \
                        sizeof((api)->member)) != 0) {                       \
            (void)fprintf(stderr, "PLATFORM missing SDL symbol %s\n", name);\
            return -1;                                                      \
        }                                                                   \
    } while (0)

static int open_sdl(struct sdl_api *api)
{
    static const char *const candidates[] = {
        "libSDL2-2.0.so.0", "libSDL2.so.0", "libSDL2.so"
    };
    const char *configured = getenv("NFSMW_SDL2_LIBRARY");
    size_t index;

    (void)memset(api, 0, sizeof(*api));
    if (configured != NULL && configured[0] != '\0') {
        api->library = dlopen(configured, RTLD_NOW | RTLD_LOCAL);
    } else {
        for (index = 0U;
             index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            api->library = dlopen(candidates[index], RTLD_NOW | RTLD_LOCAL);
            if (api->library != NULL) {
                break;
            }
        }
    }
    if (api->library == NULL) {
        (void)fprintf(stderr, "PLATFORM cannot load 32-bit SDL2: %s\n",
                      dlerror());
        return -1;
    }
    LOAD_API(api, init, "SDL_Init");
    LOAD_API(api, init_subsystem, "SDL_InitSubSystem");
    LOAD_API(api, quit, "SDL_Quit");
    LOAD_API(api, get_error, "SDL_GetError");
    LOAD_API(api, set_hint, "SDL_SetHint");
    LOAD_API(api, current_video_driver, "SDL_GetCurrentVideoDriver");
    LOAD_API(api, gl_set_attribute, "SDL_GL_SetAttribute");
    LOAD_API(api, create_window, "SDL_CreateWindow");
    LOAD_API(api, destroy_window, "SDL_DestroyWindow");
    LOAD_API(api, gl_create_context, "SDL_GL_CreateContext");
    LOAD_API(api, gl_delete_context, "SDL_GL_DeleteContext");
    LOAD_API(api, gl_set_swap_interval, "SDL_GL_SetSwapInterval");
    LOAD_API(api, gl_swap_window, "SDL_GL_SwapWindow");
    LOAD_API(api, gl_get_proc_address, "SDL_GL_GetProcAddress");
    LOAD_API(api, gl_get_drawable_size, "SDL_GL_GetDrawableSize");
    LOAD_API(api, num_joysticks, "SDL_NumJoysticks");
    LOAD_API(api, is_game_controller, "SDL_IsGameController");
    LOAD_API(api, game_controller_open, "SDL_GameControllerOpen");
    LOAD_API(api, game_controller_name, "SDL_GameControllerName");
    LOAD_API(api, game_controller_get_joystick,
             "SDL_GameControllerGetJoystick");
    LOAD_API(api, game_controller_close, "SDL_GameControllerClose");
    LOAD_API(api, game_controller_get_axis, "SDL_GameControllerGetAxis");
    LOAD_API(api, game_controller_get_button, "SDL_GameControllerGetButton");
    LOAD_API(api, joystick_num_buttons, "SDL_JoystickNumButtons");
    LOAD_API(api, joystick_get_button, "SDL_JoystickGetButton");
    LOAD_API(api, pump_events, "SDL_PumpEvents");
    LOAD_API(api, current_audio_driver, "SDL_GetCurrentAudioDriver");
    LOAD_API(api, open_audio_device, "SDL_OpenAudioDevice");
    LOAD_API(api, queue_audio, "SDL_QueueAudio");
    LOAD_API(api, queued_audio_size, "SDL_GetQueuedAudioSize");
    LOAD_API(api, pause_audio_device, "SDL_PauseAudioDevice");
    LOAD_API(api, close_audio_device, "SDL_CloseAudioDevice");
    LOAD_API(api, delay, "SDL_Delay");
    LOAD_API(api, get_ticks, "SDL_GetTicks");
    return 0;
}

static int set_gl_attributes(struct sdl_api *api)
{
    static const struct {
        int attribute;
        int value;
    } values[] = {
        { SDL_GL_CONTEXT_PROFILE_MASK_VALUE, SDL_GL_CONTEXT_PROFILE_ES_VALUE },
        { SDL_GL_CONTEXT_MAJOR_VERSION_VALUE, 2 },
        { SDL_GL_CONTEXT_MINOR_VERSION_VALUE, 0 },
        { SDL_GL_RED_SIZE_VALUE, 8 }, { SDL_GL_GREEN_SIZE_VALUE, 8 },
        { SDL_GL_BLUE_SIZE_VALUE, 8 }, { SDL_GL_ALPHA_SIZE_VALUE, 8 },
        { SDL_GL_DEPTH_SIZE_VALUE, 24 }, { SDL_GL_STENCIL_SIZE_VALUE, 8 },
        { SDL_GL_DOUBLEBUFFER_VALUE, 1 }
    };
    size_t index;

    for (index = 0U; index < sizeof(values) / sizeof(values[0]); ++index) {
        if (api->gl_set_attribute(values[index].attribute,
                                  values[index].value) != 0) {
            return -1;
        }
    }
    return 0;
}

static int load_gl_function(struct sdl_api *api, const char *name,
                            void *destination, size_t destination_size)
{
    void *address = api->gl_get_proc_address(name);

    if (address == NULL || destination_size != sizeof(address)) {
        return -1;
    }
    (void)memcpy(destination, &address, sizeof(address));
    return 0;
}

static int probe_graphics(struct sdl_api *api)
{
    sdl_window *window = NULL;
    sdl_gl_context context = NULL;
    gl_get_string_fn gl_get_string = NULL;
    gl_clear_color_fn gl_clear_color = NULL;
    gl_clear_fn gl_clear = NULL;
    const unsigned char *vendor;
    const unsigned char *renderer;
    const unsigned char *version;
    int width = 0;
    int height = 0;
    int result = -1;
    size_t frame;
    static const float colors[][3] = {
        { 0.10F, 0.20F, 0.55F },
        { 0.55F, 0.16F, 0.08F },
        { 0.08F, 0.45F, 0.20F }
    };

    if (api->init_subsystem(SDL_INIT_VIDEO_VALUE) != 0) {
        (void)fprintf(stderr, "G5-HOST FAIL SDL video: %s\n", sdl_error(api));
        return -1;
    }
    (void)api->set_hint("SDL_OPENGL_ES_DRIVER", "1");
    (void)api->set_hint("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");
    if (set_gl_attributes(api) != 0) {
        (void)fprintf(stderr, "G5-HOST FAIL GL attributes: %s\n",
                      sdl_error(api));
        return -1;
    }
    window = api->create_window(
        "NFS Most Wanted preflight", SDL_WINDOWPOS_UNDEFINED_VALUE,
        SDL_WINDOWPOS_UNDEFINED_VALUE, 640, 480,
        SDL_WINDOW_FULLSCREEN_VALUE | SDL_WINDOW_OPENGL_VALUE |
            SDL_WINDOW_SHOWN_VALUE);
    if (window == NULL) {
        (void)fprintf(stderr, "G5-HOST FAIL window: %s\n", sdl_error(api));
        return -1;
    }
    context = api->gl_create_context(window);
    if (context == NULL) {
        (void)fprintf(stderr, "G5-HOST FAIL context: %s\n", sdl_error(api));
        goto done;
    }
    if (load_gl_function(api, "glGetString", &gl_get_string,
                         sizeof(gl_get_string)) != 0 ||
        load_gl_function(api, "glClearColor", &gl_clear_color,
                         sizeof(gl_clear_color)) != 0 ||
        load_gl_function(api, "glClear", &gl_clear, sizeof(gl_clear)) != 0) {
        (void)fprintf(stderr, "G5-HOST FAIL required GLES2 symbols\n");
        goto done;
    }
    softfp_binding_api = api;
    if (nfsmw_softfp_bind_gles(resolve_softfp_gles) != 0) {
        softfp_binding_api = NULL;
        (void)fprintf(stderr, "G5-HOST FAIL softfp GLES thunk binding\n");
        goto done;
    }
    softfp_binding_api = NULL;
    api->gl_get_drawable_size(window, &width, &height);
    (void)api->gl_set_swap_interval(0);
    vendor = gl_get_string(GL_VENDOR_VALUE);
    renderer = gl_get_string(GL_RENDERER_VALUE);
    version = gl_get_string(GL_VERSION_VALUE);
    (void)printf("G5-HOST GLES vendor=%s renderer=%s version=%s drawable=%dx%d\n",
                 vendor != NULL ? (const char *)vendor : "unavailable",
                 renderer != NULL ? (const char *)renderer : "unavailable",
                 version != NULL ? (const char *)version : "unavailable",
                 width, height);
    for (frame = 0U; frame < sizeof(colors) / sizeof(colors[0]); ++frame) {
        gl_clear_color(colors[frame][0], colors[frame][1], colors[frame][2],
                       1.0F);
        gl_clear(GL_COLOR_BUFFER_BIT_VALUE);
        api->gl_swap_window(window);
        api->delay(60U);
    }
    if (width != 640 || height != 480) {
        (void)fprintf(stderr, "G5-HOST FAIL expected 640x480 drawable\n");
        goto done;
    }
    (void)printf("G5-HOST PASS SDL=%s GLES2 frames=3\n",
                 api->current_video_driver() != NULL ?
                     api->current_video_driver() : "unknown");
    result = 0;

done:
    if (context != NULL) {
        api->gl_delete_context(context);
    }
    if (window != NULL) {
        api->destroy_window(window);
    }
    return result;
}

static int probe_controller(struct sdl_api *api)
{
    int count;
    int index;

    if (api->init_subsystem(SDL_INIT_GAMECONTROLLER_VALUE) != 0) {
        (void)fprintf(stderr, "G6-HOST FAIL SDL controller: %s\n",
                      sdl_error(api));
        return -1;
    }
    count = api->num_joysticks();
    for (index = 0; index < count; ++index) {
        if (api->is_game_controller(index) != 0) {
            sdl_controller *controller = api->game_controller_open(index);

            if (controller != NULL) {
                const char *name = api->game_controller_name(controller);

                (void)printf("G6-HOST PASS controller=%s joysticks=%d\n",
                             name != NULL ? name : "unnamed", count);
                api->game_controller_close(controller);
                return 0;
            }
        }
    }
    (void)fprintf(stderr, "G6-HOST FAIL no SDL game controller (joysticks=%d)\n",
                  count);
    return -1;
}

static int probe_audio(struct sdl_api *api)
{
    struct sdl_audio_spec wanted;
    struct sdl_audio_spec obtained;
    unsigned char silence[4096];
    sdl_audio_device device;
    uint32_t queued;

    if (api->init_subsystem(SDL_INIT_AUDIO_VALUE) != 0) {
        (void)fprintf(stderr, "G8-HOST FAIL SDL audio: %s\n", sdl_error(api));
        return -1;
    }
    (void)memset(&wanted, 0, sizeof(wanted));
    (void)memset(&obtained, 0, sizeof(obtained));
    (void)memset(silence, 0, sizeof(silence));
    wanted.frequency = 44100;
    wanted.format = SDL_AUDIO_S16LSB_VALUE;
    wanted.channels = 2U;
    wanted.samples = 512U;
    device = api->open_audio_device(NULL, 0, &wanted, &obtained, 0);
    if (device == 0U) {
        (void)fprintf(stderr, "G8-HOST FAIL open audio: %s\n", sdl_error(api));
        return -1;
    }
    if (api->queue_audio(device, silence, sizeof(silence)) != 0) {
        (void)fprintf(stderr, "G8-HOST FAIL queue audio: %s\n", sdl_error(api));
        api->close_audio_device(device);
        return -1;
    }
    queued = api->queued_audio_size(device);
    api->pause_audio_device(device, 0);
    api->delay(40U);
    (void)printf("G8-HOST PASS driver=%s format=%dHz/%uch queued=%u bytes\n",
                 api->current_audio_driver() != NULL ?
                     api->current_audio_driver() : "unknown",
                 obtained.frequency,
                 (unsigned int)obtained.channels, queued);
    api->close_audio_device(device);
    return 0;
}

int nfsmw_platform_probe(struct nfsmw_platform_probe_result *result)
{
    struct sdl_api api;

    if (result == NULL) {
        return -1;
    }
    (void)memset(result, 0, sizeof(*result));
    if (open_sdl(&api) != 0) {
        return -1;
    }
    if (api.init(0U) != 0) {
        (void)fprintf(stderr, "PLATFORM SDL_Init failed: %s\n",
                      sdl_error(&api));
        (void)dlclose(api.library);
        return -1;
    }
    result->graphics = probe_graphics(&api) == 0;
    result->controller = probe_controller(&api) == 0;
    result->audio = probe_audio(&api) == 0;
    api.quit();
    (void)dlclose(api.library);
    return result->graphics && result->controller && result->audio ? 0 : -1;
}

struct persistent_runtime {
    struct sdl_api api;
    sdl_window *window;
    sdl_gl_context context;
    sdl_controller *controller;
    sdl_joystick *joystick;
    sdl_audio_device audio_device;
    gl_clear_color_fn gl_clear_color;
    gl_clear_fn gl_clear;
    gl_get_error_fn gl_get_error;
    gl_get_integer_v_fn gl_get_integer_v;
    gl_get_float_v_fn gl_get_float_v;
    gl_get_boolean_v_fn gl_get_boolean_v;
    gl_is_enabled_fn gl_is_enabled;
    gl_enable_fn gl_enable;
    gl_disable_fn gl_disable;
    gl_scissor_fn gl_scissor;
    gl_color_mask_fn gl_color_mask;
    gl_bind_framebuffer_fn gl_bind_framebuffer;
    gl_check_framebuffer_status_fn gl_check_framebuffer_status;
    gl_read_pixels_fn gl_read_pixels;
    int drawable_width;
    int drawable_height;
    unsigned int gl_error_count;
    uint32_t previous_raw_buttons;
    int go_super_raw_fallback;
    int started;
};

static struct persistent_runtime runtime;

int nfsmw_platform_runtime_start(int width, int height)
{
    int count;
    int index;
    int drawable_width = 0;
    int drawable_height = 0;

    if (runtime.started != 0) {
        return 0;
    }
    (void)memset(&runtime, 0, sizeof(runtime));
    if (open_sdl(&runtime.api) != 0 ||
        runtime.api.init(SDL_INIT_VIDEO_VALUE |
                         SDL_INIT_GAMECONTROLLER_VALUE |
                         SDL_INIT_AUDIO_VALUE) != 0) {
        (void)fprintf(stderr, "G5-RUNTIME FAIL SDL startup\n");
        goto fail;
    }
    (void)runtime.api.set_hint("SDL_OPENGL_ES_DRIVER", "1");
    (void)runtime.api.set_hint("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");
    if (set_gl_attributes(&runtime.api) != 0) {
        (void)fprintf(stderr, "G5-RUNTIME FAIL GL attributes: %s\n",
                      sdl_error(&runtime.api));
        goto fail;
    }
    runtime.window = runtime.api.create_window(
        "Need for Speed Most Wanted", SDL_WINDOWPOS_UNDEFINED_VALUE,
        SDL_WINDOWPOS_UNDEFINED_VALUE, width, height,
        SDL_WINDOW_FULLSCREEN_VALUE | SDL_WINDOW_OPENGL_VALUE |
            SDL_WINDOW_SHOWN_VALUE);
    if (runtime.window == NULL) {
        (void)fprintf(stderr, "G5-RUNTIME FAIL window: %s\n",
                      sdl_error(&runtime.api));
        goto fail;
    }
    runtime.context = runtime.api.gl_create_context(runtime.window);
    if (runtime.context == NULL) {
        (void)fprintf(stderr, "G5-RUNTIME FAIL context: %s\n",
                      sdl_error(&runtime.api));
        goto fail;
    }
    softfp_binding_api = &runtime.api;
    if (nfsmw_softfp_bind_gles(resolve_softfp_gles) != 0) {
        softfp_binding_api = NULL;
        (void)fprintf(stderr, "G5-RUNTIME FAIL GLES thunk binding\n");
        goto fail;
    }
    softfp_binding_api = NULL;
    if (load_gl_function(&runtime.api, "glClearColor",
                         &runtime.gl_clear_color,
                         sizeof(runtime.gl_clear_color)) != 0 ||
        load_gl_function(&runtime.api, "glClear", &runtime.gl_clear,
                         sizeof(runtime.gl_clear)) != 0 ||
        load_gl_function(&runtime.api, "glGetError", &runtime.gl_get_error,
                         sizeof(runtime.gl_get_error)) != 0 ||
        load_gl_function(&runtime.api, "glGetIntegerv",
                         &runtime.gl_get_integer_v,
                         sizeof(runtime.gl_get_integer_v)) != 0 ||
        load_gl_function(&runtime.api, "glGetFloatv",
                         &runtime.gl_get_float_v,
                         sizeof(runtime.gl_get_float_v)) != 0 ||
        load_gl_function(&runtime.api, "glGetBooleanv",
                         &runtime.gl_get_boolean_v,
                         sizeof(runtime.gl_get_boolean_v)) != 0 ||
        load_gl_function(&runtime.api, "glIsEnabled",
                         &runtime.gl_is_enabled,
                         sizeof(runtime.gl_is_enabled)) != 0 ||
        load_gl_function(&runtime.api, "glEnable", &runtime.gl_enable,
                         sizeof(runtime.gl_enable)) != 0 ||
        load_gl_function(&runtime.api, "glDisable", &runtime.gl_disable,
                         sizeof(runtime.gl_disable)) != 0 ||
        load_gl_function(&runtime.api, "glScissor", &runtime.gl_scissor,
                         sizeof(runtime.gl_scissor)) != 0 ||
        load_gl_function(&runtime.api, "glColorMask", &runtime.gl_color_mask,
                         sizeof(runtime.gl_color_mask)) != 0 ||
        load_gl_function(&runtime.api, "glBindFramebuffer",
                         &runtime.gl_bind_framebuffer,
                         sizeof(runtime.gl_bind_framebuffer)) != 0 ||
        load_gl_function(&runtime.api, "glCheckFramebufferStatus",
                         &runtime.gl_check_framebuffer_status,
                         sizeof(runtime.gl_check_framebuffer_status)) != 0 ||
        load_gl_function(&runtime.api, "glReadPixels",
                         &runtime.gl_read_pixels,
                         sizeof(runtime.gl_read_pixels)) != 0) {
        (void)fprintf(stderr, "G5-RUNTIME FAIL GLES diagnostics/overlay\n");
        goto fail;
    }
    (void)runtime.api.gl_set_swap_interval(1);
    runtime.api.gl_get_drawable_size(runtime.window,
                                     &drawable_width, &drawable_height);
    runtime.drawable_width = drawable_width;
    runtime.drawable_height = drawable_height;
    count = runtime.api.num_joysticks();
    for (index = 0; index < count; ++index) {
        if (runtime.api.is_game_controller(index) != 0) {
            runtime.controller = runtime.api.game_controller_open(index);
            if (runtime.controller != NULL) {
                const char *name = runtime.api.game_controller_name(
                    runtime.controller);

                runtime.joystick = runtime.api.game_controller_get_joystick(
                    runtime.controller);
                runtime.go_super_raw_fallback = name != NULL &&
                    strstr(name, "GO-Super Gamepad") != NULL;
                break;
            }
        }
    }
    runtime.started = 1;
    (void)printf("G5-RUNTIME PASS persistent GLES drawable=%dx%d\n",
                 drawable_width, drawable_height);
    (void)printf("G6-RUNTIME %s controller=%s\n",
                 runtime.controller != NULL ? "PASS" : "WARN",
                 runtime.controller != NULL ?
                   (runtime.api.game_controller_name(runtime.controller) != NULL ?
                    runtime.api.game_controller_name(runtime.controller) :
                    "unnamed") : "unavailable");
    if (runtime.joystick != NULL)
        (void)printf("G6-RUNTIME raw-buttons=%d go-super-fallback=%d\n",
                     runtime.api.joystick_num_buttons(runtime.joystick),
                     runtime.go_super_raw_fallback);
    return 0;

fail:
    nfsmw_platform_runtime_stop();
    return -1;
}

static void runtime_gl_diagnostics(unsigned int frame)
{
    unsigned int error;
    unsigned int drained = 0U;

    if (runtime.gl_get_error == NULL) return;
    while ((error = runtime.gl_get_error()) != 0U && drained < 16U) {
        ++runtime.gl_error_count;
        if (runtime.gl_error_count <= 32U ||
            runtime.gl_error_count % 100U == 0U)
            (void)printf("G5-GL ERROR frame=%u code=0x%04x count=%u\n",
                         frame, error, runtime.gl_error_count);
        ++drained;
    }
    if (frame < 10U || frame % 300U == 0U) {
        int framebuffer = 0;
        unsigned int status;

        runtime.gl_get_integer_v(GL_FRAMEBUFFER_BINDING_VALUE, &framebuffer);
        status = runtime.gl_check_framebuffer_status(GL_FRAMEBUFFER_VALUE);
        (void)printf("G5-GL STATE frame=%u framebuffer=%d status=0x%04x errors=%u\n",
                     frame, framebuffer, status, runtime.gl_error_count);
    }
}

static void runtime_draw_cursor(int x, int y)
{
    int framebuffer = 0;
    int scissor_box[4] = { 0, 0, 0, 0 };
    float clear_color[4] = { 0.0F, 0.0F, 0.0F, 0.0F };
    uint8_t color_mask[4] = { 1U, 1U, 1U, 1U };
    uint8_t scissor_enabled;
    int gl_y;

    if (runtime.drawable_width <= 0 || runtime.drawable_height <= 0) return;
    if (x < 8) x = 8;
    if (x >= runtime.drawable_width - 8) x = runtime.drawable_width - 9;
    if (y < 8) y = 8;
    if (y >= runtime.drawable_height - 8) y = runtime.drawable_height - 9;
    gl_y = runtime.drawable_height - 1 - y;

    runtime.gl_get_integer_v(GL_FRAMEBUFFER_BINDING_VALUE, &framebuffer);
    runtime.gl_get_integer_v(GL_SCISSOR_BOX_VALUE, scissor_box);
    runtime.gl_get_float_v(GL_COLOR_CLEAR_VALUE, clear_color);
    runtime.gl_get_boolean_v(GL_COLOR_WRITEMASK_VALUE, color_mask);
    scissor_enabled = runtime.gl_is_enabled(GL_SCISSOR_TEST_VALUE);

    runtime.gl_bind_framebuffer(GL_FRAMEBUFFER_VALUE, 0U);
    runtime.gl_enable(GL_SCISSOR_TEST_VALUE);
    runtime.gl_color_mask(1U, 1U, 1U, 1U);
    runtime.gl_clear_color(1.0F, 0.82F, 0.05F, 1.0F);
    runtime.gl_scissor(x - 8, gl_y - 1, 17, 3);
    runtime.gl_clear(GL_COLOR_BUFFER_BIT_VALUE);
    runtime.gl_scissor(x - 1, gl_y - 8, 3, 17);
    runtime.gl_clear(GL_COLOR_BUFFER_BIT_VALUE);

    runtime.gl_clear_color(clear_color[0], clear_color[1], clear_color[2],
                           clear_color[3]);
    runtime.gl_color_mask(color_mask[0], color_mask[1], color_mask[2],
                          color_mask[3]);
    runtime.gl_scissor(scissor_box[0], scissor_box[1], scissor_box[2],
                       scissor_box[3]);
    if (scissor_enabled == 0U) runtime.gl_disable(GL_SCISSOR_TEST_VALUE);
    runtime.gl_bind_framebuffer(GL_FRAMEBUFFER_VALUE,
                                (unsigned int)framebuffer);
}

static void runtime_capture_frame(unsigned int frame)
{
    const char *directory = getenv("NFSMW_SCREENSHOT_DIR");
    unsigned char header[18] = { 0U };
    unsigned char *pixels;
    char path[4096];
    FILE *output;
    size_t pixel_count;
    size_t index;
    int framebuffer = 0;
    int write_failed = 0;
    int width = runtime.drawable_width;
    int height = runtime.drawable_height;
    int length;

    if (directory == NULL || directory[0] == '\0' || frame < 6000U ||
        frame > 18000U || frame % 3000U != 0U || width <= 0 || height <= 0 ||
        width > 65535 || height > 65535) return;
    pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / 4U) return;
    pixels = malloc(pixel_count * 4U);
    if (pixels == NULL) return;

    runtime.gl_get_integer_v(GL_FRAMEBUFFER_BINDING_VALUE, &framebuffer);
    runtime.gl_bind_framebuffer(GL_FRAMEBUFFER_VALUE, 0U);
    runtime.gl_read_pixels(0, 0, width, height, GL_RGBA_VALUE,
                           GL_UNSIGNED_BYTE_VALUE, pixels);
    runtime.gl_bind_framebuffer(GL_FRAMEBUFFER_VALUE,
                                (unsigned int)framebuffer);
    for (index = 0U; index < pixel_count; ++index) {
        unsigned char red = pixels[index * 4U];
        pixels[index * 4U] = pixels[index * 4U + 2U];
        pixels[index * 4U + 2U] = red;
    }

    length = snprintf(path, sizeof(path), "%s/nfsmw-capture-%05u.tga",
                      directory, frame);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        free(pixels);
        return;
    }
    output = fopen(path, "wb");
    if (output == NULL) {
        (void)fprintf(stderr, "G10-CAPTURE FAIL path=%s\n", path);
        free(pixels);
        return;
    }
    header[2] = 2U;
    header[12] = (unsigned char)((unsigned int)width & 0xffU);
    header[13] = (unsigned char)(((unsigned int)width >> 8) & 0xffU);
    header[14] = (unsigned char)((unsigned int)height & 0xffU);
    header[15] = (unsigned char)(((unsigned int)height >> 8) & 0xffU);
    header[16] = 32U;
    header[17] = 8U;
    if (fwrite(header, 1U, sizeof(header), output) != sizeof(header))
        write_failed = 1;
    if (fwrite(pixels, 4U, pixel_count, output) != pixel_count)
        write_failed = 1;
    if (fclose(output) != 0) write_failed = 1;
    if (write_failed != 0) {
        (void)fprintf(stderr, "G10-CAPTURE FAIL write=%s\n", path);
    } else {
        (void)printf("G10-CAPTURE PASS frame=%u path=%s\n", frame, path);
    }
    free(pixels);
}

void nfsmw_platform_runtime_present(unsigned int frame, int cursor_x,
                                    int cursor_y, int cursor_visible)
{
    if (runtime.started != 0 && runtime.window != NULL) {
        runtime_gl_diagnostics(frame);
        runtime_capture_frame(frame);
        if (cursor_visible != 0) runtime_draw_cursor(cursor_x, cursor_y);
        runtime.api.gl_swap_window(runtime.window);
    }
}

void nfsmw_platform_runtime_delay(unsigned int milliseconds)
{
    if (runtime.started != 0) {
        runtime.api.delay((uint32_t)milliseconds);
    }
}

unsigned int nfsmw_platform_runtime_ticks(void)
{
    return runtime.started != 0 ? (unsigned int)runtime.api.get_ticks() : 0U;
}

int nfsmw_platform_runtime_input(short axes[6], unsigned char buttons[15])
{
    size_t index;
    static const int go_super_raw_map[15] = {
        1, 0, 2, 3, 12, -1, 13, 14, 15, 4, 5, 8, 9, 10, 11
    };

    if (runtime.started == 0 || runtime.controller == NULL) {
        (void)memset(axes, 0, 6U * sizeof(*axes));
        (void)memset(buttons, 0, 15U * sizeof(*buttons));
        return 0;
    }
    runtime.api.pump_events();
    for (index = 0U; index < 6U; ++index) {
        axes[index] = (short)runtime.api.game_controller_get_axis(
            runtime.controller, (int)index);
    }
    for (index = 0U; index < 15U; ++index) {
        buttons[index] = runtime.api.game_controller_get_button(
            runtime.controller, (int)index);
    }
    if (runtime.go_super_raw_fallback != 0 && runtime.joystick != NULL) {
        uint32_t raw_mask = 0U;
        int raw_count = runtime.api.joystick_num_buttons(runtime.joystick);

        if (raw_count > 32) raw_count = 32;
        for (index = 0U; index < (size_t)raw_count; ++index) {
            uint8_t pressed = runtime.api.joystick_get_button(
                runtime.joystick, (int)index);
            if (pressed != 0U) raw_mask |= 1U << index;
        }
        for (index = 0U; index < 15U; ++index) {
            int raw_index = go_super_raw_map[index];
            if (raw_index >= 0 && raw_index < raw_count &&
                (raw_mask & (1U << (unsigned int)raw_index)) != 0U)
                buttons[index] = 1U;
        }
        if (raw_mask != runtime.previous_raw_buttons) {
            (void)printf("G6-RAW buttons=0x%08x\n", raw_mask);
            runtime.previous_raw_buttons = raw_mask;
        }
    }
    return buttons[4] != 0U && buttons[6] != 0U;
}

int nfsmw_platform_runtime_audio_start(int frequency, int channels)
{
    struct sdl_audio_spec wanted;
    struct sdl_audio_spec obtained;

    if (runtime.started == 0) return -1;
    if (runtime.audio_device != 0U) return 0;
    (void)memset(&wanted, 0, sizeof(wanted));
    (void)memset(&obtained, 0, sizeof(obtained));
    wanted.frequency = frequency;
    wanted.format = SDL_AUDIO_S16LSB_VALUE;
    wanted.channels = (uint8_t)channels;
    wanted.samples = 512U;
    runtime.audio_device = runtime.api.open_audio_device(
        NULL, 0, &wanted, &obtained, 0);
    if (runtime.audio_device == 0U) {
        (void)fprintf(stderr, "G8-OPENSL FAIL audio device: %s\n",
                      sdl_error(&runtime.api));
        return -1;
    }
    runtime.api.pause_audio_device(runtime.audio_device, 0);
    (void)printf("G8-OPENSL PASS SDL output=%dHz/%uch\n",
                 obtained.frequency, (unsigned int)obtained.channels);
    return 0;
}

int nfsmw_platform_runtime_audio_queue(const void *data, unsigned int size)
{
    if (runtime.audio_device == 0U || data == NULL || size == 0U) return -1;
    return runtime.api.queue_audio(runtime.audio_device, data, size);
}

unsigned int nfsmw_platform_runtime_audio_queued(void)
{
    return runtime.audio_device != 0U ?
        (unsigned int)runtime.api.queued_audio_size(runtime.audio_device) : 0U;
}

void nfsmw_platform_runtime_stop(void)
{
    if (runtime.audio_device != 0U &&
        runtime.api.close_audio_device != NULL) {
        runtime.api.close_audio_device(runtime.audio_device);
    }
    if (runtime.controller != NULL &&
        runtime.api.game_controller_close != NULL) {
        runtime.api.game_controller_close(runtime.controller);
    }
    if (runtime.context != NULL && runtime.api.gl_delete_context != NULL) {
        runtime.api.gl_delete_context(runtime.context);
    }
    if (runtime.window != NULL && runtime.api.destroy_window != NULL) {
        runtime.api.destroy_window(runtime.window);
    }
    if (runtime.api.quit != NULL) {
        runtime.api.quit();
    }
    if (runtime.api.library != NULL) {
        (void)dlclose(runtime.api.library);
    }
    (void)memset(&runtime, 0, sizeof(runtime));
}
