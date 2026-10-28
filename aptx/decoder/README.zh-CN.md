# 解码器

公开接口：`include/aptx/decoder/decoder.h`，CMake 目标：`aptx_aa::decoder`。

`src/api` 管理句柄、前包 carry、容量重试及收尾；`src/codec` 实现有界比特读取、
Huffman/比特分配解析和整数重建；`src/dsp` 实现浮点 FIR 合成与 overlap-add。
私有头在 `src/include`，测试在 `tests`。

共享预测器、抖动、初始化和编码系数依赖 `../common`，不链接编码器。
算法和当前厂商 PCM 对照边界见 [`docs/zh-CN/decode.md`](../../docs/zh-CN/decode.md)。
