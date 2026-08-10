#ifndef NFSMW_COMPAT_BRIDGE_H
#define NFSMW_COMPAT_BRIDGE_H

#include <stdint.h>

void nfsmw_compat_init(void);
void nfsmw_compat_finalize(void);
int nfsmw_compat_signal_selftest(void);

int nfsmw_bionic_setjmp(void *environment) __attribute__((returns_twice));
int nfsmw_bionic_sigsetjmp(void *environment, int save_signal_mask)
    __attribute__((returns_twice));
void nfsmw_bionic_longjmp(void *environment, int value)
    __attribute__((noreturn));
void nfsmw_bionic_siglongjmp(void *environment, int value)
    __attribute__((noreturn));

uint32_t nfsmw_bionic_signal_mask_capture(void);
void nfsmw_bionic_signal_mask_restore(uint32_t mask);
void nfsmw_bionic_longjmp_error(void) __attribute__((noreturn));
uintptr_t nfsmw_compat_resolve(const char *name);

#endif
