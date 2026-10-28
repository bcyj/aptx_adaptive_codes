# 代码目录与组件

参照 `D:\lhdc\lhdc_codes` 的组件归属与 `src/api`、算法、私有头、公共头、测试分层，
本项目使用以下布局。所有实现都在本项目维护，LHDC 仅作结构和测试方法参考。

```text
aptx_aa/
  aptx/
    common/
      include/aptx/common/types.h     公开类型、状态码和公共工具接口
      src/api/util.c                 错误文本、byte swap
      src/codec/params.c             编码变体和参数检查
      src/dsp/{engine,predict}.c      共用状态初始化、预测器、抖动
      src/tables/codec_tables.c       编解码共用数值表的唯一存储
      src/include/                   私有类型、整数运算、表声明
    encoder/
      include/aptx/encoder/encoder.h  编码与流控制公开 API
      src/api/encoder.c              句柄、输入处理、封包周期与码率控制
      src/codec/                     分析滤波、比特分配、量化、打包
      src/stream/                    档位、队列、欠额控制及模式表
      src/include/                   编码器私有结构和模式表声明
      tests/                         编码、流控制和 Rust 差分验证
    decoder/
      include/aptx/decoder/decoder.h  解码公开 API
      src/api/decoder.c              句柄、跨包缓存、容量重试、收尾
      src/codec/                     比特流解析、反量化、子带重建
      src/dsp/                       合成滤波器及其只读系数
      src/include/                   解码私有结构、合成接口、验证钩子
      tests/                         阶段、WAV 工具和损坏输入验证
    platform/windows/               仓库内置 MSBuild 工程和 DLL 导出表
  include/aptx_aa.h                  兼容原调用者的聚合头
  aptx_adaptive_windows.sln         Win32/x64 Visual Studio 解决方案
  tools/build.py                    直接 GCC/ar 构建入口
  tools/cli/                        编码/解码文件工具
  tests/support/                    共用 Python 测试辅助代码
  cmake/                            总体构建与测试注册
  docs/                             目录与测试方法说明
```

公开包含路径：

```c
#include <aptx/encoder/encoder.h> /* 编码应用 */
#include <aptx/decoder/decoder.h> /* 解码应用 */
#include <aptx/common/types.h>   /* 共用类型 */
#include <aptx_aa.h>             /* 原聚合包含方式继续可用 */
```

对应 CMake 目标为 `aptx_aa::encoder`、`aptx_aa::decoder`、`aptx_aa::common` 和
组合目标 `aptx_aa::codec`。编码器和解码器各自依赖 common，彼此无需链接。
组合库由同一组对象组成，共用的预测器、初始化、数值表不重复定义。
使用原聚合头时链接组合目标；独立组件目标提供对应组件的公开头及 common 类型。

简易构建生成：`libaptx_aa_common.a`、`libaptx_aa_encoder.a`、
`libaptx_aa_decoder.a`、`libaptx_aa.a`、`aptx_aa.dll`，以及两个 CLI。
直接使用静态库时，将对应组件库放在 `libaptx_aa_common.a` 之前。

本轮没有引入平台特有代码或额外的通用 FFT 模块，因此没有建立空的平台/数学目录。
若后续增加 SIMD、内核或分配器适配，再按实际实现归属添加。

## 构建验证

生成后的数值表直接纳入源码管理。`tools/build.py` 直接使用各组件的源码清单，不依赖迁移清单或代码生成脚本。

```powershell
python tools/build.py --audio-common-root ..\audio_codec_common
```

默认构建会生成 common、encoder、decoder、组合库以及两个 CLI 程序。裁剪后的发布版已经使用 GCC 和 `-Wall -Wextra -Wpedantic` 成功重建。后续直接维护组件目录，不再从旧目录重新生成。

`aptx/common/src/api/runtime.c`和公开的`aptx/common/memory.h`负责宿主内存所有权，
零CRT模式的内存原语也在该单元；整数数学契约直接引用外部公共math树，不复制代码。
算法状态与接口仍分别归encoder/decoder，文件CLI的宿主分配示例在`tools/cli/host_allocator.h`。
