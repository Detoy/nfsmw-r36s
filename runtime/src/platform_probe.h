#ifndef NFSMW_PLATFORM_PROBE_H
#define NFSMW_PLATFORM_PROBE_H

struct nfsmw_platform_probe_result {
    int graphics;
    int controller;
    int audio;
};

int nfsmw_platform_probe(struct nfsmw_platform_probe_result *result);

int nfsmw_platform_runtime_start(int width, int height);
void nfsmw_platform_runtime_present(unsigned int frame, int cursor_x,
                                    int cursor_y, int cursor_visible);
void nfsmw_platform_runtime_delay(unsigned int milliseconds);
unsigned int nfsmw_platform_runtime_ticks(void);
int nfsmw_platform_runtime_input(short axes[6], unsigned char buttons[15]);
int nfsmw_platform_runtime_audio_start(int frequency, int channels);
int nfsmw_platform_runtime_audio_queue(const void *data, unsigned int size);
unsigned int nfsmw_platform_runtime_audio_queued(void);
void nfsmw_platform_runtime_stop(void);

#endif
