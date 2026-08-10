#ifndef NFSMW_JNI_BRIDGE_H
#define NFSMW_JNI_BRIDGE_H

#include "elf32_loader.h"

#include <stddef.h>
#include <stdint.h>

int nfsmw_jni_startup(const struct elf32_image *nimble_image,
                      const struct elf32_image *app_image,
                      char *error, size_t error_size);
int nfsmw_apply_app_patches(const struct elf32_image *app_image,
                            char *error, size_t error_size);
int nfsmw_jni_run(const struct elf32_image *fmod_image,
                  const struct elf32_image *app_image,
                  char *error, size_t error_size);
void nfsmw_jni_shutdown(void);
int nfsmw_jni_bitmap_info(void *bitmap, uint32_t information[5]);
int nfsmw_jni_bitmap_lock(void *bitmap, void **pixels);
int nfsmw_jni_bitmap_unlock(void *bitmap);

#endif
