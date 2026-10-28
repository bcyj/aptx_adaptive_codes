# 编码器

公开接口：`include/aptx/encoder/encoder.h`，CMake 目标：`aptx_aa::encoder`。

`src/api` 管理句柄、PCM 消费、封包与码率反馈；`src/codec` 实现分析滤波、
位分配、量化及位流打包；`src/stream` 负责 PCM 队列、质量档位和周期控制。
私有结构及声明在 `src/include`，测试在 `tests`。

公共预测器、抖动、初始化和系数表依赖 `../common`，不链接解码器。
共享参考数据仍在项目根目录 `tests/vectors`。
