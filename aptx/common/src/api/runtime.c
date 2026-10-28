#include "aa_runtime.h"
#include <limits.h>

#if APTX_AA_EXTERNAL_ALLOCATOR_ONLY

/* Implements aptx_aa_default_allocator. */
const aptx_aa_allocator *aptx_aa_default_allocator(void) {
    return NULL;
}
#else

/* Implements host_alloc. */
static void *host_alloc(void *context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}

/* Implements host_free. */
static void host_free(void *context, void *memory) {
    (void)context;
    free(memory);
}

/* Implements aptx_aa_default_allocator. */
const aptx_aa_allocator *aptx_aa_default_allocator(void) {
    static const aptx_aa_allocator allocator = {NULL, host_alloc, host_free};
    return &allocator;
}
#endif

/* Implements aptx_aa_no_crt. */
int aptx_aa_no_crt(void) {
    return APTX_AA_NO_CRT;
}

/* Implements aptx_aa_driver_resources. */
int aptx_aa_driver_resources(void) {
    return APTX_AA_DRIVER_RESOURCES;
}

/* Implements aptx_aa_external_allocator_only. */
int aptx_aa_external_allocator_only(void) {
    return APTX_AA_EXTERNAL_ALLOCATOR_ONLY;
}

/* Implements aa_memory_alloc. */
void *aa_memory_alloc(const aptx_aa_allocator *allocator, size_t bytes) {
    if (!allocator)
        allocator = aptx_aa_default_allocator();
    return audio_memory_alloc(allocator, bytes);
}

/* Implements aa_memory_calloc. */
void *aa_memory_calloc(const aptx_aa_allocator *allocator, size_t n, size_t bytes) {
    if (!allocator)
        allocator = aptx_aa_default_allocator();
    return audio_memory_calloc(allocator, n, bytes);
}

/* Implements aa_memory_free. */
void aa_memory_free(void *p) {
    audio_memory_free(p);
}
