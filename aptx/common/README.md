# Common Codec Implementation

Public types are declared in `include/aptx/common/types.h`; the CMake target is
`aptx_aa::common`.

This component owns shared parameter validation, fixed-width integer semantics,
state initialization, prediction, dither, and read-only codec tables. Encoder
and decoder instances keep separate mutable state. Large tables have a single
definition in `.c` files and private headers contain declarations only.

The common component does not depend on encoder or decoder interfaces.

[Chinese documentation](README.zh-CN.md)
