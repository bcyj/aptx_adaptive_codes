#include "aptx_aa_kernel_memory.h"

/* Implements kernel_allocate. */
static void *APTX_AA_MEMORY_CALL kernel_allocate(void *context, size_t bytes) {
    const aptx_aa_kernel_memory_context *owner = context;
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, owner->tag);
}

/* Implements kernel_release. */
static void APTX_AA_MEMORY_CALL kernel_release(void *context, void *memory) {
    const aptx_aa_kernel_memory_context *owner = context;
    ExFreePoolWithTag(memory, owner->tag);
}

/* Implements aptx_aa_kernel_allocator_init. */
NTSTATUS aptx_aa_kernel_allocator_init(aptx_aa_allocator *allocator,
                                       aptx_aa_kernel_memory_context *context, ULONG tag) {
    if (!allocator || !context || !tag || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    context->tag = tag;
    allocator->context = context;
    allocator->allocate = kernel_allocate;
    allocator->release = kernel_release;
    return STATUS_SUCCESS;
}

/* Implements aptx_aa_kernel_stack_available. */
BOOLEAN aptx_aa_kernel_stack_available(SIZE_T host_reserve_bytes) {
    if (host_reserve_bytes > MAXULONG_PTR - APTX_AA_KERNEL_CODEC_STACK_BUDGET)
        return FALSE;
    return IoGetRemainingStackSize() >= host_reserve_bytes + APTX_AA_KERNEL_CODEC_STACK_BUDGET;
}
