#ifndef APTX_AA_MEMORY_H
#define APTX_AA_MEMORY_H
#include <audio_common/memory.h>
#include <stddef.h>
#ifndef APTX_AA_DRIVER_RESOURCES
#define APTX_AA_DRIVER_RESOURCES 0
#endif
#ifndef APTX_AA_NO_CRT
#define APTX_AA_NO_CRT 0
#endif
#ifndef APTX_AA_EXTERNAL_ALLOCATOR_ONLY
#define APTX_AA_EXTERNAL_ALLOCATOR_ONLY APTX_AA_NO_CRT
#endif
#if APTX_AA_DRIVER_RESOURCES &&                                                            \
    (!APTX_AA_NO_CRT || !APTX_AA_EXTERNAL_ALLOCATOR_ONLY || !defined(NDEBUG))
#error "Driver resources require NO_CRT=1, EXTERNAL_ALLOCATOR_ONLY=1 and Release/NDEBUG"
#endif
#if APTX_AA_NO_CRT != 0 && APTX_AA_NO_CRT != 1
#error "APTX_AA_NO_CRT must be 0 or 1"
#endif
#if APTX_AA_EXTERNAL_ALLOCATOR_ONLY != 0 && APTX_AA_EXTERNAL_ALLOCATOR_ONLY != 1
#error "APTX_AA_EXTERNAL_ALLOCATOR_ONLY must be 0 or 1"
#endif
#if APTX_AA_NO_CRT && !APTX_AA_EXTERNAL_ALLOCATOR_ONLY
#error "NO_CRT requires EXTERNAL_ALLOCATOR_ONLY=1"
#endif
#define APTX_AA_MEMORY_CALL AUDIO_MEMORY_CALL
#define APTX_AA_WORKSPACE_ALIGNMENT AUDIO_MEMORY_ALIGNMENT
#ifdef __cplusplus
extern "C" {
#endif

/* Uses the host-allocator ABI shared by the supported audio codecs. */
typedef AudioAllocator aptx_aa_allocator;

/* Returns the default host allocator available to this build, or NULL. */
const aptx_aa_allocator *aptx_aa_default_allocator(void);

/* Reports whether the build is configured without a C runtime. */
int aptx_aa_no_crt(void);

/* Reports whether fixed driver-resource limits are enabled. */
int aptx_aa_driver_resources(void);

/* Reports whether callers must provide an allocator or workspace. */
int aptx_aa_external_allocator_only(void);
#ifdef __cplusplus
}
#endif
#endif
