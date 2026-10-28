# aptX Adaptive Decoder

The decoder is included in the same C11 library. Its public entry points are in `include/aptx_aa.h`, and the executable is `build/aptx_aa_decode.exe`.
It supports stereo configs 0/1/3/4 produced by the upstream encoder, including R1/R2 music modes.
It does not restore lossy encoding to the original PCM; information discarded by quantization cannot be recovered.

The decoder implementation is now under `aptx/decoder`. Its standalone public header is `aptx/decoder/include/aptx/decoder/decoder.h`, and it can be linked independently as `aptx_aa::decoder`.
`src/api` manages handles and cross-packet buffering, `src/codec` handles bitstreams and integer reconstruction, and `src/dsp` performs synthesis filtering.
Shared predictors and numeric tables come from `aptx/common`. The original aggregate header remains available.

Actual aptX bitstream processing is based on this project's encoder and coefficient tables, plus upstream test-only `crates/aptx-adaptive/src/codec/decode.rs`.

## Decode Pipeline

```text
codec packet (raw / byte swap detected automatically)
  → validate packet header, length, and padding
  → exp-Golomb carry length and cross-packet concatenation
  → absolute/differential bit-allocation Huffman decode
  → subband quantization-code Huffman decode and raw extension bits
  → signed quantization code, inverse quantization, and step-size update
  → add current prediction, then update predictor and dither state
  → subband synthesis FIR and overlap-add
  → interleaved stereo PCM with 24 significant bits
```

Each Huffman composer symbol decodes two subband blocks.
After bit allocation is read, an ordinary quantization frame contains at most 64 blocks. Config 3 has a separate block-count escape field when allocation is all zero.
Quantization codes wider than 9 bits and allocation escapes reverse the encoder tables. The two-bit composer for the highest four-band subband uses a dedicated table.

The encoder places overflow bytes from the previous logical packet at the beginning of the next physical packet.
The decoder therefore buffers one packet and completes it when the following packet arrives before producing output.
This prevents a half quantization code that crosses a packet boundary from being misread as a new frame.
At finish, only complete code pairs are decoded; carry that was never transmitted cannot be invented.

## Inverse Quantization and Prediction

First sign-extend the bitstream field to `q`. Let `e=max(bits-9,0)` and `c=q>>e`, and select inverse-quantization table index `j` according to the sign.
The reconstructed residual `r` is computed from the threshold table, extended low bits, current step size, and dither.
Step adaptation, saturation, overflow, and rounding rules match the encoder's local reconstruction.

The synthesized subband value is `sat24(prediction + r)`, while the predictor is updated with `r`.
The output PCM or complete subband value must not replace the residual in the update.
The dither seed is formed from the first two quantization codes in the current block and used by later blocks.
Config 1 follows the upstream low-power path, including its highest-subband history reconstruction behavior.

This portion has been compared field-by-field against independent reference captures: quantization codes, allocation, reconstructed residuals, and updated predictions match exactly.
The reconstructed subband was also checked against the saturated reference `pred_in + quant_recon`.

## Synthesis Filter Derivation

The encoder analysis side can be expressed as a decimating subband FIR.
After fixed-point rounding, overflow, and saturation are removed, the analysis filter bank approximately satisfies a constant-gain paraunitary condition within its normal-amplitude linear range.
A causal synthesis filter is obtained by using the time-reversed transposed filter and correcting gain:

- Four-band QMF tree: combine two levels of analysis FIR and use a transpose gain correction of 8.
- Eight-band CMFB: expand the front end and DCT. The 8×16 modulation matrix satisfies `T·Tᵀ ≈ 8I`, and the complete filter uses a transpose gain correction of 64.

The compile-time-selected read-only `double` / Q29 FIR tables are pre-generated and checked into the source tree.
At runtime, the decoder performs weighted subband sums, reverse-time indexing, and circular overlap-add, then rounds and saturates to 24-bit PCM.
There is currently no SIMD optimization.

| config | Combined FIR length | Analysis+synthesis delay, samples per channel |
|---|---:|---:|
| 0 | 94 | 90 |
| 1 | 70 | 66 |
| 3 | 256 | 248 |
| 4 | 128 | 120 |

These delays were measured in this implementation and exclude packet buffering, cycle waiting, and Bluetooth transport.
Delay metadata exposed by other upstream interfaces has a different purpose and must not be mixed with these values.
The file tool trims this startup delay by default and emits the synthesis history at the end.
The library API retains the original timeline for the caller to manage.

When reference `analysis_out` is passed directly into the synthesizer, aligned PCM reconstruction SNR is approximately 92–113 dB. Config 4 and 88.2 kHz were also tested.
**This is not a quality metric for the final lossy bitstream.** Low-bitrate streams deliberately omit bands, so recovered PCM cannot match the original input sample-for-sample.

The synthesizer is derived from the encoder and has both floating-point and fixed-point paths.
Neither has been independently compared against vendor-decoder PCM.
Exact integer reconstruction state therefore does not justify a claim that final PCM is bit-exact with the vendor decoder.

## Command Line

```powershell
# Detect sample rate, encoding config, packet length, and byte swap; write PCM24 WAV by default
.\build\aptx_aa_decode.exe -i output.aptxaa -o decoded.wav

# PCM16 WAV
.\build\aptx_aa_decode.exe -i output.aptxaa -o decoded16.wav --bits 16

# Right-aligned signed 24-bit values in little-endian int32 slots, matching encoder --raw s32le24
.\build\aptx_aa_decode.exe -i output.aptxaa -o decoded.s32le --raw

# Retain analysis/synthesis delay
.\build\aptx_aa_decode.exe -i output.aptxaa -o delayed.wav --keep-delay
```

Input must begin at the first codec packet.
WAV32 writes the full 32-bit range by shifting PCM24 left eight bits; `--raw` remains right-aligned and does not apply that shift.
RIFF WAV output is limited to 4 GiB.

## C API

```c
aptx_aa_config config = {48000, 2, APTX_AA_HQ, APTX_AA_R2};
aptx_aa_dec *decoder = aptx_aa_decoder_create(&config);
/* create_variant(0/1/3/4, sample_rate) is also available. Constructors return NULL on failure. */

size_t samples = 0;
int rc = aptx_aa_decoder_push(decoder, packet, packet_bytes,
                               pcm, pcm_capacity, &samples);
/* On success, consumes the entire packet and writes 'samples' interleaved samples.
 * The first packet writes zero samples. On OUTPUT_TOO_SMALL, 'samples' reports the
 * required capacity and state is unchanged; retry the same packet with a larger PCM
 * buffer. pcm=NULL, capacity=0 may be used to query required capacity.
 */

rc = aptx_aa_decoder_finish(decoder, pcm, pcm_capacity, &samples);
/* OUTPUT_TOO_SMALL retry is also supported. Emits the final complete quantization
 * code and synthesis tail. A second finish writes zero samples; call reset before a
 * new stream, or create a new instance for the matching configuration.
 */
aptx_aa_decoder_destroy(decoder);
```

Output capacity is counted in `int32_t` samples, not bytes or stereo frames.
The example illustrates call order; real applications must check constructor and call results and provide input/output storage.
See the capacity-retry logic in `tools/cli/aptx_aa_decode.c`.

Inverse quantization and predictor state use integers. PCM synthesis can select floating point or fixed point at compile time.
Decoder instances are independent, and calls on the same instance must be serialized.
Normal push/finish calls do not allocate heap memory; instance creation prepares state and Huffman trees.

## Limitations and Validation

- Stereo configs 0/1/3/4 are supported. Speech, R3/R4, Lossless, and other channel modes are not.
- RTP/A2DP/SPF containers are not parsed. Resampling, packet-loss concealment, and automatic discontinuity tracking are not provided.
- Carry across one following physical packet can be recovered. If an unusually small packet causes carry to span multiple later packets, `APTX_AA_UNSUPPORTED_STREAM` is returned instead of fabricating PCM.
- Starting in the middle of a stream or losing a packet destroys prediction history that reset alone cannot recover; re-establish the stream.
- Data has no CRC guarantee. Syntax checks reject some corrupt input but cannot prove that accepted data was never modified or lost. A corrupt packet may be recognized only by the next push/finish.
- EOF may omit part of the last code pair, which finish discards. Physical packet truncation is reported by the file tool. The final recoverable sample count need not equal the source PCM length for every input duration.

Run:

```powershell
python aptx/decoder/tests/validate_decoder.py build/aptx_aa.dll
python aptx/decoder/tests/validate_decoder_cli.py build/aptx_aa_decode.exe
```

## Selecting Fixed-Point Decode at Build Time

`APTX_AA_DECODER_FIXED_POINT=1` is the default fixed-point synthesis mode and keeps the complete decode path integer.
Set it to `0` to restore floating point. The public API, PCM format, delay, and stream-state behavior remain the same.

```powershell
# Direct builds in separate directories
python tools/build.py --decoder-float --output-dir build-float
python tools/build.py --decoder-fixed-point --output-dir build-fixed

# CMake: OFF is floating point, ON is fixed point (default)
cmake -S . -B build-fixed-cmake -DAPTX_AA_DECODER_FIXED_POINT=ON
cmake --build build-fixed-cmake --config Release
```

Existing CMake build directories retain the option in their cache.
Pass `-DAPTX_AA_DECODER_FIXED_POINT=ON` explicitly when switching to fixed point, or `OFF` for the floating-point comparison.

Manual builds must define `-DAPTX_AA_DECODER_FIXED_POINT=1` for every decoder translation unit, including `synthesis_tables.c`.
After changing the macro, rebuild the complete decoder library; do not mix object files using different coefficient types.
`aptx_aa_decoder_fixed_point()` reports the linked library's actual build mode independently of the caller's header macro.

The fixed-point path uses signed 32-bit coefficients with 29 fractional bits, 64-bit multiply-accumulate, and 64-bit overlap state.
It rounds only after all contributions are accumulated, rounds half-integers toward positive infinity, and finally saturates to 24 bits, matching the original floating-point rule.
The sum of absolute coefficient magnitudes per phase gives a conservative bound, verified by static assertions in generated tables:

```text
8388608 * max_phase_sum(abs(Q29_coefficients)) + 2^28 < INT64_MAX
PCM = sat24((accumulator + 2^28) >> 29)
```

Inverse quantization/prediction saturates input subbands to 24 bits; that range is a precondition of the overflow proof above.
The theoretical difference bound relative to the floating-point path is one 24-bit PCM LSB for configs 0/1/4 and two LSB for config 3.
This is a fixed-point implementation of the existing synthesis filter and does not establish that a vendor fixed-point filter or final PCM was reproduced.
See [build options](build-options.md) for the fixed-point selection and driver-oriented configuration.

## Optional No-CRT Library

`APTX_AA_NO_CRT=1` (default 0) selects external host allocation and the existing common-math integer helpers.
Use `aptx_aa_decoder_create_with_allocator()` or the matching variant entry point; the normal destroy function still releases the instance.
Original constructors return NULL because the default heap is unavailable.
See [build options](build-options.md) for the allocator callback contract and no-CRT configuration.
The parser scratch area is stored in the decoder instance, adding 24,768 instance bytes and reducing push-call stack use without changing PCM or capacity-retry behavior.

Later driver-resource work compressed the bit buffer, shared Huffman trees, and added caller-owned initialization.
The current x64 decoder workspace is 21,608 bytes. Query the actual size through the API.
