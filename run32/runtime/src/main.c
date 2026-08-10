#include "elf32_loader.h"
#include "compat_bridge.h"
#include "initializer_trace.h"
#include "jni_bridge.h"
#include "platform_probe.h"
#include "relocation_probe.h"
#include "symbol_probe.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { MODULE_COUNT = 5, PATH_CAPACITY = 4096 };

static const char *const module_names[MODULE_COUNT] = {
    "libc++_shared.so",
    "libfmodex.so",
    "libfmodevent.so",
    "libNimble.so",
    "libapp.so",
};

int main(int argc, char **argv)
{
    struct elf32_image images[MODULE_COUNT];
    struct nfsmw_symbol_probe_stats symbol_stats;
    struct nfsmw_platform_probe_result platform_result;
    struct nfsmw_relocation_probe_stats relocation_stats;
    char error[ELF32_LOADER_ERROR_CAPACITY];
    char path[PATH_CAPACITY];
    size_t mapped = 0U;
    size_t index;
    bool constructors_started = false;
    bool constructors_completed = false;
    int result = 1;

    (void)setvbuf(stdout, NULL, _IONBF, 0U);
    (void)setvbuf(stderr, NULL, _IONBF, 0U);
    nfsmw_compat_init();
    (void)memset(images, 0, sizeof(images));
    if (argc != 2) {
        (void)fprintf(stderr, "Usage: %s <android-libs-directory>\n", argv[0]);
        return 2;
    }
    (void)printf("=== G2 MAP REGRESSION ===\n");
    for (index = 0U; index < MODULE_COUNT; ++index) {
        const int length = snprintf(path, sizeof(path), "%s/%s", argv[1],
                                    module_names[index]);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            (void)fprintf(stderr, "Module path is too long\n");
            goto done;
        }
        (void)printf("mapping[%zu]=%s\n", index, path);
        if (elf32_map(&images[index], path, error, sizeof(error)) != 0) {
            (void)fprintf(stderr, "FAIL map: %s\n", error);
            goto done;
        }
        mapped += 1U;
        if (elf32_describe(&images[index]) != 0) {
            goto done;
        }
    }
    (void)printf("G2 PASS: mapped %zu ARM32 modules without guest execution\n",
                 mapped);

    (void)printf("=== G3 PROVIDER CENSUS (NO GUEST EXECUTION) ===\n");
    if (nfsmw_probe_symbols(images, MODULE_COUNT, &symbol_stats,
                            error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "G3-CENSUS FAIL: %s\n", error);
        goto done;
    }
    (void)printf("G3-CENSUS PASS: target provider data captured; full "
                 "relocation remains gated on ABI bridges\n");

    (void)printf("=== G5/G6/G8 HOST PREFLIGHTS ===\n");
    if (nfsmw_platform_probe(&platform_result) != 0) {
        (void)fprintf(stderr,
                      "HOST-PREFLIGHT FAIL graphics=%d controller=%d audio=%d\n",
                      platform_result.graphics, platform_result.controller,
                      platform_result.audio);
        goto done;
    }
    (void)printf("HOST-PREFLIGHT PASS graphics=%d controller=%d audio=%d\n",
                 platform_result.graphics, platform_result.controller,
                 platform_result.audio);

    (void)printf("=== G3 ARM REL RELOCATION PHASE A ===\n");
    if (nfsmw_relocation_probe(images, MODULE_COUNT, &relocation_stats,
                               error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "G3-REL FAIL: %s\n", error);
        goto done;
    }
    if (nfsmw_apply_app_patches(&images[MODULE_COUNT - 1U],
                                error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "G3-PATCH FAIL: %s\n", error);
        goto done;
    }
    if (getenv("NFSMW_RUN_CONSTRUCTORS") != NULL) {
        size_t initializer_count = 0U;

        if (relocation_stats.unresolved_relocations != 0U) {
            (void)fprintf(stderr,
                          "G4-CTOR REFUSED: %zu relocations are unresolved\n",
                          relocation_stats.unresolved_relocations);
            goto done;
        }
        (void)printf("=== G4 BIONIC SIGNAL/JUMP SELFTEST ===\n");
        if (nfsmw_compat_signal_selftest() != 0) {
            (void)fprintf(stderr, "G4-SIGNAL FAIL: bridge self-test failed\n");
            goto done;
        }
        (void)printf("G4-SIGNAL PASS: sigaction, sigprocmask and "
                     "siglongjmp\n");
        constructors_started = true;
        (void)printf("=== G4 DEPENDENCY-ORDERED CONSTRUCTORS ===\n");
        for (index = 0U; index < MODULE_COUNT; ++index) {
            if (nfsmw_run_initializers(&images[index], &initializer_count,
                                       error, sizeof(error)) != 0) {
                (void)fprintf(stderr, "G4-CTOR FAIL: %s\n", error);
                goto done;
            }
        }
        (void)printf("G4-CTOR PASS initializers=%zu\n", initializer_count);
        constructors_completed = true;
        if (getenv("NFSMW_RUN_JNI") != NULL) {
            if (nfsmw_jni_startup(&images[MODULE_COUNT - 2U],
                                  &images[MODULE_COUNT - 1U],
                                  error, sizeof(error)) != 0) {
                (void)fprintf(stderr, "G4-JNI FAIL: %s\n", error);
                goto done;
            }
            if (getenv("NFSMW_RUN_GAME") != NULL &&
                nfsmw_jni_run(&images[1U],
                              &images[MODULE_COUNT - 1U],
                              error, sizeof(error)) != 0) {
                (void)fprintf(stderr, "G5/G7 GAME FAIL: %s\n", error);
                goto done;
            }
            if (getenv("NFSMW_RUN_GAME") != NULL) {
                nfsmw_jni_shutdown();
                (void)printf("G7 PROCESS EXIT PASS guest teardown deferred\n");
                (void)fflush(NULL);
                _exit(0);
            }
        }
    }
    (void)printf("COMBINED PASS: mapping, provider census, host preflights "
                 "and relocation phase A completed\n");
    result = 0;

done:
    if (constructors_started) {
        nfsmw_jni_shutdown();
        (void)printf("G4-CTOR finalizing __cxa_atexit registrations\n");
        nfsmw_compat_finalize();
        if (constructors_completed) {
            size_t finalizer_count = 0U;
            bool finalizers_ok = true;

            (void)printf("=== G4 REVERSE-ORDER FINALIZERS ===\n");
            index = MODULE_COUNT;
            while (index != 0U) {
                index -= 1U;
                if (nfsmw_run_finalizers(&images[index], &finalizer_count,
                                         error, sizeof(error)) != 0) {
                    (void)fprintf(stderr, "G4-DTOR FAIL: %s\n", error);
                    result = 1;
                    finalizers_ok = false;
                    break;
                }
            }
            if (finalizers_ok) {
                (void)printf("G4-DTOR PASS finalizers=%zu\n",
                             finalizer_count);
            }
        }
        (void)fflush(NULL);
        _exit(result);
    }
    while (mapped != 0U) {
        mapped -= 1U;
        elf32_unmap(&images[mapped]);
    }
    nfsmw_relocation_release_hosts();
    return result;
}
