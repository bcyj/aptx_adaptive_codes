#ifndef APTX_AA_KERNEL_MEMORY_H
#define APTX_AA_KERNEL_MEMORY_H
#include <aptx/common/memory.h>
#include <ntddk.h>
#if !APTX_AA_DRIVER_RESOURCES
#error "The nonpaged driver adapter requires APTX_AA_DRIVER_RESOURCES=1"
#endif
typedef struct aptx_aa_kernel_memory_context {
    ULONG tag;
} aptx_aa_kernel_memory_context;

_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS
    aptx_aa_kernel_allocator_init(aptx_aa_allocator *, aptx_aa_kernel_memory_context *, ULONG tag);
#define APTX_AA_KERNEL_CODEC_STACK_BUDGET 4096u

_IRQL_requires_max_(DISPATCH_LEVEL) BOOLEAN
    aptx_aa_kernel_stack_available(SIZE_T host_reserve_bytes);
#endif
