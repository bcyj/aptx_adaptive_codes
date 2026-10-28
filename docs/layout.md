# Source Layout and Components

Following the component ownership and public-header, private-header, `src/api`, algorithm, and test layering used by `D:\lhdc\lhdc_codes`, this project uses the layout below. All implementations are maintained in this project; LHDC was referenced only for structure and test methodology.

```text
aptx_aa/
  aptx/
    common/
      include/aptx/common/types.h     Public types, status codes, and common utility interfaces
      src/api/util.c                  Error text and byte swap
      src/codec/params.c              Encoding variants and parameter validation
      src/dsp/{engine,predict}.c      Shared state initialization, predictor, and dither
      src/tables/codec_tables.c       Sole storage for codec-wide numeric tables
      src/include/                   Private types, integer operations, and table declarations
    encoder/
      include/aptx/encoder/encoder.h  Public encoding and stream-control API
      src/api/encoder.c              Handles, input processing, packet cycles, and bitrate control
      src/codec/                     Analysis filter, bit allocation, quantization, and packing
      src/stream/                    Levels, queue, deficit control, and mode tables
      src/include/                   Private encoder structures and mode-table declarations
      tests/                         Encoder, stream-control, and Rust differential validation
    decoder/
      include/aptx/decoder/decoder.h  Public decoder API
      src/api/decoder.c              Handles, cross-packet buffering, capacity retry, and finish
      src/codec/                     Bitstream parsing, inverse quantization, and subband reconstruction
      src/dsp/                       Synthesis filter and its read-only coefficients
      src/include/                   Private decoder structures, synthesis interfaces, and validation hooks
      tests/                         Stage validation, WAV tools, and malformed-input validation
    platform/windows/               Checked-in MSBuild projects and DLL export list
  include/aptx_aa.h                  Aggregate header retained for existing callers
  aptx_adaptive_windows.sln         Win32/x64 Visual Studio solution
  tools/build.py                    Direct GCC/ar build entry point
  tools/cli/                        Encoder/decoder file tools
  tests/support/                    Shared Python test helpers
  cmake/                            Top-level build and test registration
  docs/                             Layout and test-method documentation
```

Public include paths:

```c
#include <aptx/encoder/encoder.h> /* Encoding applications */
#include <aptx/decoder/decoder.h> /* Decoding applications */
#include <aptx/common/types.h>   /* Shared types */
#include <aptx_aa.h>             /* Original aggregate include remains supported */
```

The corresponding CMake targets are `aptx_aa::encoder`, `aptx_aa::decoder`, `aptx_aa::common`, and the combined `aptx_aa::codec` target.
Encoder and decoder each depend on common and do not need to link each other.
The combined library is assembled from the same object set; shared predictors, initialization, and numeric tables are not defined twice.
Link the combined target when using the aggregate header. Standalone component targets expose the matching public headers and common types.

The simple build produces `libaptx_aa_common.a`, `libaptx_aa_encoder.a`, `libaptx_aa_decoder.a`, `libaptx_aa.a`, `aptx_aa.dll`, and two CLI executables.
When linking the static libraries directly, place the selected component library before `libaptx_aa_common.a`.

This reorganization did not add platform-specific code or another generic FFT module, so no empty platform or math directories were created.
If SIMD, kernel, or allocator adapters are added later, create directories according to actual ownership.

## Build Validation

Generated numeric tables are checked into the source tree. `tools/build.py` uses the component source lists directly and does not depend on migration manifests or code-generation scripts.

```powershell
python tools/build.py --audio-common-root ..\audio_codec_common
```

The default build produces common, encoder, decoder, and combined libraries plus both CLI programs. The trimmed release was rebuilt successfully with GCC using `-Wall -Wextra -Wpedantic`. Maintain the component directories directly; do not regenerate them from older layouts.

`aptx/common/src/api/runtime.c` and public `aptx/common/memory.h` define host memory ownership; the no-CRT memory primitives are also in that translation unit.
Integer math contracts directly reference the external common math tree rather than copying code.
Algorithm state and interfaces remain owned by encoder/decoder, while the file CLI's host-allocation example is in `tools/cli/host_allocator.h`.
