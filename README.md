# aptx_aa — aptX Adaptive C Codec

This library was created for another open-source A2DP driver project of mine and has been tested with several headsets, including Xiaomi Line Free. Gemini and ChatGPT were used extensively to generate code and to generate and translate documentation and comments. I plan to review the material manually when time permits.

This project is a C11 rewrite based on the `crates/aptx-adaptive` implementation in [AKS-Logic/aptx-adaptive-linux](https://github.com/AKS-Logic/aptx-adaptive-linux), with an independently written decoder. The referenced upstream revision is `f8a090875ae2d23721f0ed175f77228f190444f1`.
Building and running does not require Rust, vendor shared libraries, DSP firmware, or the upstream repository. Coefficients and Huffman tables are compiled into `aptx/common/src/tables/codec_tables.c`.

The upstream project uses the MIT License. Its license text is retained in [`LICENSES/AKS-Logic_LICENSES`](LICENSES/AKS-Logic_LICENSES), and detailed provenance is provided in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
This is an independent implementation and is neither affiliated with nor endorsed by Qualcomm.

## Supported Scope

| Item | Support |
|---|---|
| Input | Stereo interleaved signed 24-bit integers, right-aligned in `int32_t` |
| R1 | 44.1/48 kHz, HQ/LL, encoding config 0 |
| R2 | 44.1/48 kHz HQ config 3 and LL config 4 |
| High sample rates | 88.2/96 kHz HQ config 3 with either decoder level; high-rate LL is unsupported |
| Research interface | Explicit config 0/1/3/4 creation; config 1 is the upstream low-power mode |
| Stream control | Periodic packetization, deficit feedback, bitrate changes, quality-level switching, and state reset |
| File tools | Stereo RIFF/WAV PCM16/24/32 and corresponding raw PCM |

R1/R2 are receiver compatibility levels, while `config 0/1/3/4` are internal encoding variants; they are not the same numbering system.
`aptx_aa_row.cfg` is transport metadata from the upstream mode table and cannot be passed directly to `aptx_aa_create_variant()`.

The project implements music encoding and matching PCM decoding. It does not include speech encoding, R3/R4, aptX Lossless, a PipeWire plugin, or Bluetooth negotiation.

The decoder supports the same configs 0/1/3/4. Entropy decoding, inverse quantization, and predictor state have been compared exactly against reference-stage data.
PCM synthesis uses an FIR derived from the analysis filter, defaults to fixed point, and can select floating point at compile time. **Sample-for-sample identity with a vendor decoder has not been validated.**

The code follows the LHDC release layout with `aptx/encoder`, `aptx/decoder`, `aptx/common`, and `aptx/platform`, each containing public headers, private headers, and algorithm directories.
The aggregate header remains supported; new callers may include `<aptx/encoder/encoder.h>` or `<aptx/decoder/decoder.h>` and link the matching component.
See the [layout documentation](docs/layout.md) for complete ownership details.

## Encoder Tool

Run from this directory:

```powershell
# Read the sample rate from the WAV header; default to R1/HQ/level 3
.\build\aptx_aa_encode.exe -i music.wav -o music.aptxaa

# 96 kHz stereo WAV, R2/HQ
.\build\aptx_aa_encode.exe -i music96.wav -o music96.aptxaa --decoder r2 --profile hq

# Raw 48 kHz PCM: each int32 uses only the low 24 bits, sign-extended
.\build\aptx_aa_encode.exe -i input.s32le -o output.aptxaa --raw s32le24 --rate 48000

# Reproduce repository reference data and separately save each packet length and forced-flush flag
.\build\aptx_aa_encode.exe -i tests\vectors\parity\mix48000.s32le -o build\example.bin --raw s32le24 --rate 48000 --sizes build\example.sizes

.\build\aptx_aa_encode.exe --help
```

Decode codec packet files to WAV:

```powershell
.\build\aptx_aa_decode.exe -i music.aptxaa -o decoded.wav
.\build\aptx_aa_decode.exe -i music96.aptxaa -o decoded96.wav --bits 24
.\build\aptx_aa_decode.exe --help
```

The decoder automatically detects sample rate, packet length, encoding variant, and byte swap.
It writes 24-bit stereo WAV by default and trims analysis/synthesis delay. `--raw` writes right-aligned 24-bit values in little-endian int32 slots.
Input must begin with the first codec packet and must not contain RTP/A2DP transport headers.

Raw format differences: `s16le` is ordinary 16-bit PCM; `s24le` uses three bytes per sample; `s32le` is full 32-bit PCM and is shifted right by eight before encoding; `s32le24` is right-aligned, sign-extended 24-bit data.
32-bit PCM in WAV is treated as full 32-bit data. Floating-point WAV is rejected, and no resampling is performed.

The output file is a concatenation of fixed-size raw codec packets with no generic container, RTP, or A2DP header.
Packets default to 656 bytes; the mode table or `--packet` selects the exact size.
`--wire` only swaps each adjacent byte pair. The caller must still add transport headers and complete Bluetooth negotiation.

To reproduce upstream behavior, periodic packetization is driven by roughly 1 ms input chunks by default, with no additional EOF zero-padding or forced packet.
`--flush` can close an already partial encoder packet, but it does not pad queued tail samples or codec carry into a complete ending.
The encoder retains real-time stream semantics, so a file ending may leave less than one encoding block or packet cycle buffered.
Input chunk size also affects packet boundaries; keep `--chunk`, cycle, bitrate, and packet size identical for byte-for-byte comparisons.

## Build

Only a C11 compiler is required. Numeric tables are generated already; import scripts do not run during builds.

```powershell
# Most convenient on this machine; Python only orchestrates gcc/ar
python tools/build.py

# Select a toolchain
python tools/build.py --cc gcc.exe --ar ar.exe
```

Visual Studio 2019/MSBuild projects are included for hosted Win32 and x64 builds. Keep
`aptx_adaptive_codes` and `audio_codec_common` as sibling directories, then run:

```powershell
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=x64
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=Win32
```

The solution builds the common, encoder, and decoder static libraries, the combined
`aptx_aa.dll`, and both command-line tools. Set `/p:AptxAaDecoderFixedPoint=0` only when a
floating-point decoder comparison is required. No-CRT and SYS builds remain under the CMake/WDK
configuration because the checked-in MSBuild solution targets ordinary hosted Windows programs.

CMake is also available for C11-capable GCC/Clang and modern MSVC.
The local MinGW shared-library CMake build and all four checked-in MSBuild configurations were tested; other platforms require their own build validation.

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build-cmake --config Release
cd build-cmake
ctest --output-on-failure -C Release
```

Use `-DBUILD_SHARED_LIBS=OFF` for static libraries; this is the default.
Windows CMake shared builds produce `libaptx_aa.dll`, while the simple build script produces `aptx_aa.dll`.

## C API

Real-time callers should normally use `aptx_aa_stream`, which includes queueing and cycle control:

```c
#include "aptx_aa.h"

aptx_aa_config config = {48000, 2, APTX_AA_HQ, APTX_AA_R1};
size_t budget = aptx_aa_packet_budget(895); /* Subtract 8 bytes from link MTU. */
aptx_aa_stream *stream = aptx_aa_stream_create(&config, budget);
if (!stream) { /* Unsupported configuration/budget, or out of memory. */ }

/* Submit a small PCM block. pcm_samples is the total L+R sample count. */
uint8_t packet[APTX_AA_MAX_PACKET];
size_t written = 0;
int rc = aptx_aa_stream_push(stream, pcm, pcm_samples,
                              packet, sizeof(packet), &written);
if (rc == APTX_AA_OK && written) {
    /* Send packet[0..written). This API has already applied the 16-bit byte swap. */
}

/* Request level 4 at the next packet boundary; only same-packet-size levels are accepted. */
rc = aptx_aa_stream_request_level(stream, 4);
/* Apply immediately if already at a packet boundary; returns 1/0. */
int applied = aptx_aa_stream_apply_level_now(stream);

aptx_aa_stream_destroy(stream);
```

The example shows call order; the application supplies `pcm`, `pcm_samples`, and transport logic.
A successful `push` consumes every submitted sample. A full queue returns `APTX_AA_QUEUE_FULL` without consuming that input.
Call `push(stream, NULL, 0, ...)` to process already queued data. Each call returns at most one packet and does not pad insufficient PCM.
The default level is 3. Upstream rules map requested level 1 to level 2; research builds may disable this mapping.
The controller stores at most 30,720 interleaved samples.

Use the lower-level API when controlling packet cycles and carry directly or reproducing reference data:

```c
aptx_aa_encoder *encoder = aptx_aa_create(&config, 364000, 656);
aptx_aa_progress p;
rc = aptx_aa_encode(encoder, pcm0, pcm0_samples, pcm1, pcm1_samples,
                    packet, sizeof(packet), &p);
/* After success, remove only p.consumed interleaved samples and retain the rest.
 * p.written is zero or one packet size. Output is not byte-swapped.
 * When the cycle expires, aptx_aa_flush may close the pending packet.
 */
aptx_aa_destroy(encoder);
```

`aptx_aa_set_bitrate()` clamps to upstream ranges: 200000–600000 bps for configs 0/1/4 and 90000–1000000 bps for config 3.
The low-level packet size may be 12–1032 bytes and must be four-byte aligned, but packets that are too small or use mismatched cycles can accumulate carry/PCM.
Normal applications should use the mode table and validate budgets.
`aptx_aa_table_row()` rejects HQ budgets below the encoder minimum bitrate and LL modes above budget.

Except for the explicitly boolean `apply_level_now`, state APIs return 0 on success and a negative value on error. Constructors return NULL on failure.
`OUTPUT_TOO_SMALL`, invalid parameters, and a full queue are rejected during preflight without advancing encoder state.
Reset or destroy an object after out-of-memory or internal encoding errors.
Different objects have independent state; callers must serialize concurrent access to the same object.

## Validation

```powershell
python aptx/encoder/tests/validate_vectors.py build/aptx_aa.dll
python aptx/encoder/tests/validate_stream.py build/aptx_aa.dll
python aptx/encoder/tests/validate_cli.py build/aptx_aa_encode.exe
python aptx/decoder/tests/validate_decoder.py build/aptx_aa.dll
python aptx/decoder/tests/validate_decoder_cli.py build/aptx_aa_decode.exe

# Optional: compile the original Rust library directly and verify additional boundary inputs without modifying it
python aptx/encoder/tests/validate_reference.py build/aptx_aa_encode.exe
```

## Source Structure and Licensing

`aptx/encoder/src/codec` implements analysis filtering, bit allocation, quantization, and packetization. `src/api/encoder.c` manages encoder handles and packet-level bitrate control. `src/stream` manages the mode table, PCM queue, and level switching.
Each component has its own `CMakeLists.txt` and `tests`.

`aptx/decoder/src/api` manages decoder state, cross-packet buffering, and capacity retry. `src/codec` parses bitstreams, performs inverse quantization, and reconstructs subbands. `src/dsp` performs synthesis filtering and overlap-add.
Synthesis coefficients are pre-generated from the existing analysis coefficients and checked into the source tree; Python is not needed during builds.
Shared predictors, dither, and numeric tables exist only once in `aptx/common`.

The decoder uses fixed-point synthesis by default with `python tools/build.py`.
For a floating-point comparison, use `python tools/build.py --decoder-float --output-dir build-float` or CMake `-DAPTX_AA_DECODER_FIXED_POINT=OFF`.
See the [fixed-point decoder section](docs/decode.md#selecting-fixed-point-decode-at-build-time).

For the optional no-CRT library, use `python tools/build.py --no-crt --output-dir build-nocrt` or CMake `-DAPTX_AA_NO_CRT=ON`.
It reuses the existing common `audio_math` integer contracts and requires instances to receive an explicit host allocator. Normal SDK defaults are unchanged.
MSBuild Win32/x64 and Android ARM32/ARM64 have been tested without `-fno-builtin` or `-ffreestanding`; see [build options](docs/build-options.md).

`.sys` hosts may select fixed workspaces, no processing-time allocation, and lower stack use with `--driver-resources` or CMake `APTX_AA_DRIVER_RESOURCES=ON`; the adapter uses nonpaged pool.

The no-CRT hotspots caused by common-math small bit-pattern copies have been removed, and history buffers now move by int32 samples. The same-sample quick test is close to CRT performance.

Generated numeric tables are checked into the source tree.
The project retains the upstream MIT license and provenance in `LICENSES/AKS-Logic_LICENSES`, and `UPSTREAM_LEGAL.md`.
