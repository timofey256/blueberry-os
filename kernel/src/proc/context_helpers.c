#include <drivers/csr.h>
#include <proc/context.h>
#include <proc/thread_internal.h>

void context_init(context_t* ctx, uintptr_t stack_top) {
    memset(ctx, 0, sizeof(*ctx));

    unative_t s = csr_read(sstatus);

    s |= CSR_SSTATUS_SIE;
    s |= CSR_SSTATUS_SUM;

    ctx->sstatus = s;

    ctx->sp = (unative_t)stack_top;
    ctx->ra = (unative_t)thread_trampoline;
}