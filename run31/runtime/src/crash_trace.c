#include "crash_trace.h"

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#if defined(__GLIBC__) && defined(__arm__)
#include <ucontext.h>

static size_t append_text(char *buffer, size_t position, const char *text)
{
    while (*text != '\0' && position < 255U) buffer[position++] = *text++;
    return position;
}

static size_t append_hex(char *buffer, size_t position, uintptr_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned int shift;
    position = append_text(buffer, position, "0x");
    for (shift = 28U; shift <= 28U && position < 255U; shift -= 4U)
        buffer[position++] = digits[(value >> shift) & 15U];
    return position;
}

static void crash_handler(int signal_number, siginfo_t *information,
                          void *context_pointer)
{
    const ucontext_t *context = context_pointer;
    char buffer[256];
    size_t length = 0U;

    length = append_text(buffer, length, "G-CRASH signal=");
    if (signal_number >= 0 && signal_number <= 99) {
        if (signal_number >= 10) buffer[length++] =
            (char)('0' + signal_number / 10);
        buffer[length++] = (char)('0' + signal_number % 10);
    }
    length = append_text(buffer, length, " address=");
    length = append_hex(buffer, length,
                        (uintptr_t)(information != NULL ?
                            information->si_addr : NULL));
    length = append_text(buffer, length, " pc=");
    length = append_hex(buffer, length,
                        (uintptr_t)context->uc_mcontext.arm_pc);
    length = append_text(buffer, length, " lr=");
    length = append_hex(buffer, length,
                        (uintptr_t)context->uc_mcontext.arm_lr);
    length = append_text(buffer, length, " sp=");
    length = append_hex(buffer, length,
                        (uintptr_t)context->uc_mcontext.arm_sp);
    buffer[length++] = '\n';
    (void)write(STDERR_FILENO, buffer, length);
    _exit(128 + signal_number);
}

int nfsmw_crash_trace_install(void)
{
    static const int signals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE };
    struct sigaction action;
    size_t index;

    (void)memset(&action, 0, sizeof(action));
    action.sa_sigaction = crash_handler;
    action.sa_flags = (int)(SA_SIGINFO | SA_RESETHAND);
    (void)sigemptyset(&action.sa_mask);
    for (index = 0U; index < sizeof(signals) / sizeof(signals[0]); ++index) {
        if (sigaction(signals[index], &action, NULL) != 0) return -1;
    }
    return 0;
}
#else
int nfsmw_crash_trace_install(void) { return 0; }
#endif
