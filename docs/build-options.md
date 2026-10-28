# Build Options and LHDC/SYS Integration

aptX Adaptive options follow the component-selection and resource-configuration model used by the
LHDC project. When built as a standalone top-level project, the encoder, decoder, combined library,
and command-line tools are enabled by default, while tests are disabled. When included as a
subdirectory of the LHDC superproject, defaults for components, the combined library, tests, driver
resources, and external-allocation-only mode follow the corresponding `LHDC_*` options.

| CMake option | Standalone default | Meaning |
|---|---:|---|
| `APTX_AA_BUILD_ENCODER` | `ON` | Build `aptx_aa::encoder` |
| `APTX_AA_BUILD_DECODER` | `ON` | Build `aptx_aa::decoder` |
| `APTX_AA_BUILD_BUNDLE` | `ON` | Build `aptx_aa::codec` from the selected components |
| `APTX_AA_BUILD_TOOLS` | top-level `ON`, subproject `OFF` | Build encoder and decoder command-line tools |
| `APTX_AA_BUILD_TESTS` | `OFF` | Build and register CTest tests |
| `APTX_AA_EXTERNAL_ALLOCATOR_ONLY` | `OFF` | Disable the default CRT heap; allow only an explicit allocator or workspace |
| `APTX_AA_NO_CRT` | follows driver resources | Use the no-CRT compile and link contract |
| `APTX_AA_DRIVER_RESOURCES` | `OFF` | Select no-CRT, external-allocation-only, fixed-point decoding, and bounded-resource paths together |
| `APTX_AA_DECODER_FIXED_POINT` | `ON` | Use fixed-point decoder synthesis |
| `AUDIO_COMMON_ROOT` | adjacent `../audio_codec_common` | Path to the shared core and math source tree |

`APTX_AA_EXTERNAL_ALLOCATOR_ONLY` is separate from `APTX_AA_NO_CRT`: the former only disables the
default heap and still allows the final SYS build to use a selected CRT; the latter also enables
freestanding compilation constraints. `DRIVER_RESOURCES=ON` requires `NO_CRT=ON`,
`EXTERNAL_ALLOCATOR_ONLY=ON`, fixed-point decoding, and Release/NDEBUG.

## Recommended Configuration for One SYS

Like the LHDC release, the aptX source calls the standard `memcpy`, `memset`, `memmove`, and
`memcmp` interfaces directly. The library does not define private byte loops or same-named C
runtime symbols. The compiler should generate intrinsics or local implementations when possible;
remaining external helpers are resolved once by the final SYS toolchain:

```cmake
set(LHDC_DRIVER_RESOURCES ON CACHE BOOL "" FORCE)
set(LHDC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LHDC_BUILD_BUNDLE OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/lhdc_codes lhdc_build)

set(AUDIO_COMMON_ROOT "path/to/audio_codec_common" CACHE PATH "" FORCE)
set(AUDIO_COMMON_NO_CRT ON CACHE BOOL "" FORCE)
set(AUDIO_COMMON_DRIVER_RESOURCES ON CACHE BOOL "" FORCE)
add_subdirectory("${AUDIO_COMMON_ROOT}" audio_common_build)
set(APTX_AA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(APTX_AA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APTX_AA_BUILD_BUNDLE OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/aptx_adaptive_codes aptx_build)

target_link_libraries(my_driver_objects PRIVATE lhdc::encoder aptx_aa::encoder)
```

This order remains compatible with an LHDC development tree that still contains its built-in
`math` directory: the common repository reuses the existing `audio_math` target. After LHDC
migration is complete, the final superproject should add `audio_codec_common` before both codecs,
so math and core are each defined exactly once by the common repository.

For transmit-only use, set both `LHDC_BUILD_DECODER=OFF` and `APTX_AA_BUILD_DECODER=OFF`. For
receive-only use, disable both encoder options. Component static targets propagate their
`aptx_aa::common` dependency, so callers do not need to list the common library manually.

## Direct Builds Without CMake

`tools/build.py` provides the corresponding arguments: `--no-encoder`, `--no-decoder`,
`--no-bundle`, `--no-tools`, and `--external-allocator-only`. Like the CMake build, the direct
build does not define private `memcpy`, `memset`, or other runtime symbols.

## Visual Studio/MSBuild

`aptx_adaptive_windows.sln` provides ordinary hosted Win32 and x64 builds with Visual Studio 2019
and the v142 toolset. The solution expects `audio_codec_common` beside this repository and contains
projects for the three component static libraries, the combined DLL, and both CLI programs.

```powershell
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=x64
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=Win32
```

`AptxAaDecoderFixedPoint` defaults to `1` and may be set to `0` for a floating-point comparison.
These checked-in projects deliberately represent the normal CRT-backed Windows profile. Use the
CMake options above and the final WDK host project for no-CRT or SYS integration.

