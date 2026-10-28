# Decoder

The public API is `include/aptx/decoder/decoder.h`; the CMake target is
`aptx_aa::decoder`.

`src/api` manages instances, cross-packet carry, capacity retry, and stream
finish. `src/codec` implements bounded bit reading, Huffman and bit-allocation
parsing, and integer reconstruction. `src/dsp` implements fixed-point or
floating-point FIR synthesis and overlap-add. Private headers live under
`src/include`; component tests live under `tests`.

Shared prediction, dither, initialization, and tables come from `../common`.
The decoder does not link the encoder. See [decoder design](../../docs/decode.md).

[Chinese documentation](README.zh-CN.md)
