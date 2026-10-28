#ifndef APTX_AA_EXAMPLE_HOST_ALLOCATOR_H
#define APTX_AA_EXAMPLE_HOST_ALLOCATOR_H
#include <aptx/common/memory.h>
#include <stdlib.h>

/* Implements example_allocate. */
static void *APTX_AA_MEMORY_CALL example_allocate(void *context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}

/* Implements example_release. */
static void APTX_AA_MEMORY_CALL example_release(void *context, void *memory) {
    (void)context;
    free(memory);
}
static const aptx_aa_allocator example_allocator = {NULL, example_allocate, example_release};
#endif
