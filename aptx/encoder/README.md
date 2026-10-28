# Encoder

The public API is `include/aptx/encoder/encoder.h`; the CMake target is
`aptx_aa::encoder`.

`src/api` manages instances, PCM consumption, packet completion, and bitrate
feedback. `src/codec` implements analysis filtering, bit allocation,
quantization, and bitstream packing. `src/stream` owns the PCM queue, quality
levels, and period control. Private structures and declarations live under
`src/include`; component tests live under `tests`.

Shared prediction, dither, initialization, and tables come from `../common`.
The encoder does not link the decoder.

[Chinese documentation](README.zh-CN.md)
