#ifndef AA_RUNTIME_H
#define AA_RUNTIME_H
#include <aptx/common/memory.h>
#include <stdint.h>

#include <string.h>
#if !APTX_AA_NO_CRT
#include <stdlib.h>
#endif

/* Implements aa_memory_alloc. */
void *aa_memory_alloc(const aptx_aa_allocator *, size_t);

/* Implements aa_memory_calloc. */
void *aa_memory_calloc(const aptx_aa_allocator *, size_t, size_t);

/* Implements aa_memory_free. */
void aa_memory_free(void *);
#endif
