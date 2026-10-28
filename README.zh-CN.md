# aptx_aa — aptX Adaptive C 编解码器

此库本质上是我为我另一个开源A2DP驱动项目所制造的，在多个耳机中测试通过（如小米Line Free）。过程中大量使用Gemini和ChatGPT生成代码，文档、注释全部用Gemini和ChatGPT生成和翻译，等我有时间再手动校对。

本项目参考
[AKS-Logic/aptx-adaptive-linux](https://github.com/AKS-Logic/aptx-adaptive-linux)
中的 `crates/aptx-adaptive` 实现进行 C11 改写，并自主编写解码器，所参考的上游版本为
`f8a090875ae2d23721f0ed175f77228f190444f1`。构建和运行不依赖 Rust、厂商
共享库、DSP 固件或上游仓库；系数和 Huffman 表已经编译到
`aptx/common/src/tables/codec_tables.c` 中。

上游项目采用 MIT 许可证，其许可证原文保存在
[`LICENSES/AKS-Logic_LICENSES`](LICENSES/AKS-Logic_LICENSES)，详细来源说明见
[`THIRD_PARTY_NOTICES.zh-CN.md`](THIRD_PARTY_NOTICES.zh-CN.md)。本项目为独立实现，与 Qualcomm 不存在隶属或背书关系。

## 支持范围

| 内容 | 支持情况 |
|---|---|
| 输入 | 双声道、交错排列的 24 位有符号整数，右对齐存放于 `int32_t` |
| R1 | 44.1/48 kHz，HQ/LL，编码 config 0 |
| R2 | 44.1/48 kHz HQ config 3、LL config 4 |
| 高采样率 | 88.2/96 kHz HQ config 3，两种 decoder level 均可；不支持高采样率 LL |
| 研究接口 | 显式创建 config 0/1/3/4；config 1 是上游的低功耗模式 |
| 流控制 | 周期封包、欠额反馈、码率调整、质量档位切换、状态重置 |
| 文件工具 | 双声道 RIFF/WAV PCM16/24/32，以及对应原始 PCM |

R1/R2 是接收端兼容级别，`config 0/1/3/4` 是内部编码变体，两者不是同一编号。
`aptx_aa_row.cfg` 则是上游模式表中的传输元数据，不能直接传给 `aptx_aa_create_variant()`。

本项目实现音乐编码及对应的 PCM 解码，不包含语音编码、R3/R4、aptX Lossless、PipeWire 插件或蓝牙协商。

新增解码器支持相同的 config 0/1/3/4；熵解码、反量化和预测器状态已与参考阶段数据精确比较。PCM 合成使用从分析滤波器推导的 FIR，默认定点，可在编译期选择浮点，**尚未验证与厂商解码器逐样本一致**。

代码已按 LHDC 发布版的结构分为 `aptx/encoder`、`aptx/decoder`、`aptx/common` 和 `aptx/platform`，各有公开头、私有头和算法目录。原聚合头继续兼容；新调用可分别包含 `<aptx/encoder/encoder.h>` 或 `<aptx/decoder/decoder.h>`，并链接相应组件。完整归属见 [目录说明](docs/zh-CN/layout.md)。

## 使用编码工具

在本目录运行：

```powershell
# 读取 WAV 头中的采样率，默认 R1/HQ/档位 3
.\build\aptx_aa_encode.exe -i music.wav -o music.aptxaa

# 96 kHz 双声道 WAV，R2/HQ
.\build\aptx_aa_encode.exe -i music96.wav -o music96.aptxaa --decoder r2 --profile hq

# 原始 48 kHz PCM：每个 int32 只使用低 24 位，符号扩展
.\build\aptx_aa_encode.exe -i input.s32le -o output.aptxaa --raw s32le24 --rate 48000

# 重现仓库参考数据，另存每包长度和是否强制 flush
.\build\aptx_aa_encode.exe -i tests\vectors\parity\mix48000.s32le -o build\example.bin --raw s32le24 --rate 48000 --sizes build\example.sizes

.\build\aptx_aa_encode.exe --help
```

解码上述 codec 包文件为 WAV：

```powershell
.\build\aptx_aa_decode.exe -i music.aptxaa -o decoded.wav
.\build\aptx_aa_decode.exe -i music96.aptxaa -o decoded96.wav --bits 24
.\build\aptx_aa_decode.exe --help
```

解码器自动检测采样率、包长、编码变体和 byte swap。默认输出 24 位双声道 WAV，并裁去分析/合成延迟；`--raw` 输出右对齐 24 位数值的小端 int32 槽。它需要从流的第一个 codec 包开始读取，不接受 RTP/A2DP 传输头。

原始格式的区别：`s16le` 是普通 16 位，`s24le` 每样本 3 字节，`s32le` 是完整的 32 位 PCM（编码前右移 8 位），`s32le24` 是右对齐并符号扩展的 24 位数据。WAV 的 32 位 PCM 按完整 32 位处理；不接受浮点 WAV，也不做重采样。

输出文件是固定大小的原始 codec 包串联，没有通用容器头、RTP 或 A2DP 头。默认每包 656 字节，具体大小由模式表或 `--packet` 决定。`--wire` 只做相邻两个字节交换，仍然需要调用方添加传输头并完成蓝牙协商。

为了复现上游行为，默认按约 1 ms 输入分块驱动周期封包，EOF 时不额外补零或强制封包。`--flush` 只能关闭编码器中已有的未完成包，不会把队列尾部和 codec carry 自动补成完整结尾。此编码器沿用实时流语义，文件尾部可能保留不足一个编码块或封包周期的数据。输入分块大小也会影响包边界；逐字节比较时要保持 `--chunk`、周期、码率和包长相同。

## 构建

只需 C11 编译器；数值表已生成，不需要运行导入脚本。

```powershell
# 本机最方便的方式；Python 仅编排 gcc/ar
python tools/build.py

# 指定工具链
python tools/build.py --cc gcc.exe --ar ar.exe
```

项目提供 Visual Studio 2019/MSBuild 工程，用于普通 Win32 和 x64 宿主构建。保持
`aptx_adaptive_codes` 与 `audio_codec_common` 为同级目录，然后执行：

```powershell
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=x64
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=Win32
```

解决方案会生成 common、encoder、decoder 三个静态库、组合 `aptx_aa.dll` 和两个命令行工具。
仅在需要浮点解码对照时传入 `/p:AptxAaDecoderFixedPoint=0`。零 CRT 和 SYS 构建继续使用
CMake/WDK 配置；仓库中的 MSBuild 解决方案面向普通 Windows 宿主程序。

也提供 CMake，可用于支持 C11 的 GCC/Clang 或现代 MSVC。已实际验证本机 MinGW 共享库 CMake 构建，以及仓库内置 MSBuild 的全部四种配置；其他平台需要自行编译验证。

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build-cmake --config Release
cd build-cmake
ctest --output-on-failure -C Release
```
静态库构建用
`-DBUILD_SHARED_LIBS=OFF`，这是默认值。Windows CMake 共享库文件名为 `libaptx_aa.dll`；简易构建脚本生成的文件名为 `aptx_aa.dll`。

## C API

实时调用优先使用带队列和周期控制的 `aptx_aa_stream`：

```c
#include "aptx_aa.h"

aptx_aa_config config = {48000, 2, APTX_AA_HQ, APTX_AA_R1};
size_t budget = aptx_aa_packet_budget(895); /* 从链路 MTU 扣除 8 字节 */
aptx_aa_stream *stream = aptx_aa_stream_create(&config, budget);
if (!stream) { /* 配置或包预算不支持，或内存不足 */ }

/* 每次送入一小块 PCM。pcm_samples 是左右声道样本总数。 */
uint8_t packet[APTX_AA_MAX_PACKET];
size_t written = 0;
int rc = aptx_aa_stream_push(stream, pcm, pcm_samples,
                              packet, sizeof(packet), &written);
if (rc == APTX_AA_OK && written) {
    /* 发送 packet[0..written)。此 API 已完成 16-bit byte swap。 */
}

/* 请求档位 4，在下次包结束时生效；只允许不改变包长的档位。 */
rc = aptx_aa_stream_request_level(stream, 4);
/* 如果刚好处在包边界，可立即应用；此函数返回 1/0。 */
int applied = aptx_aa_stream_apply_level_now(stream);

aptx_aa_stream_destroy(stream);
```

上面的代码展示调用顺序，`pcm`、`pcm_samples` 和发送逻辑由应用提供。成功的 `push` 接管所有输入样本；队列满返回 `APTX_AA_QUEUE_FULL` 且不接管本次输入。用 `push(stream, NULL, 0, ...)` 处理已经排队的数据，每次最多返回一个包，不会填充不足的 PCM。默认档位为 3，请求档位 1 按上游规则映射为 2；研究时可关闭这个映射。控制器内部最多保存 30720 个交错样本。

需要自行控制包周期、处理 carry 和重现参考数据时，使用底层接口：

```c
aptx_aa_encoder *encoder = aptx_aa_create(&config, 364000, 656);
aptx_aa_progress p;
rc = aptx_aa_encode(encoder, pcm0, pcm0_samples, pcm1, pcm1_samples,
                    packet, sizeof(packet), &p);
/* 成功后仅移除 p.consumed 个交错样本；保留其余输入供下次调用。
 * p.written 为 0 或一个包的字节数。输出未经 byte swap。
 * 周期到期时，可调用 aptx_aa_flush 关闭待输出的包。
 */
aptx_aa_destroy(encoder);
```

`aptx_aa_set_bitrate()` 会按上游范围夹取码率：config 0/1/4 为 200000–600000 bps，config 3 为 90000–1000000 bps。底层包长允许 12–1032 字节、4 字节对齐，但过小的包或不匹配的周期可能使 carry/PCM 积压；正常应用应使用模式表并检查预算。`aptx_aa_table_row()` 会拒绝低于编码器最小码率的 HQ 预算及超预算的 LL 模式。

除明确返回布尔值的 `apply_level_now` 外，状态接口成功返回 0，错误返回负数。构造函数失败返回 NULL。`OUTPUT_TOO_SMALL`、参数错误和队列满在检查阶段返回，不推进编码状态。内存不足或内部编码错误后应 reset 或销毁对象。不同对象状态独立，同一对象的并发访问由调用方串行化。

## 验证

```powershell
python aptx/encoder/tests/validate_vectors.py build/aptx_aa.dll
python aptx/encoder/tests/validate_stream.py build/aptx_aa.dll
python aptx/encoder/tests/validate_cli.py build/aptx_aa_encode.exe
python aptx/decoder/tests/validate_decoder.py build/aptx_aa.dll
python aptx/decoder/tests/validate_decoder_cli.py build/aptx_aa_decode.exe

# 可选：直接编译原 Rust 库，核对额外边界输入；不修改原仓库
python aptx/encoder/tests/validate_reference.py build/aptx_aa_encode.exe
```

## 源码结构与许可

`aptx/encoder/src/codec` 实现分析滤波、比特分配、量化和封包；`src/api/encoder.c` 管理编码句柄与包级码率控制；`src/stream` 管理模式表、PCM 队列与档位切换。每个组件均有自己的 `CMakeLists.txt` 和 `tests`。

`aptx/decoder/src/api` 管理解码状态、跨包缓冲与容量重试；`src/codec` 解析码流、反量化和重建子带；`src/dsp` 进行合成滤波和 overlap-add。合成系数已从现有分析系数预生成并提交到源码树，构建时无需 Python。共享预测器、抖动和数值表只在 `aptx/common` 保留一份。

解码器默认使用定点合成：`python tools/build.py`；浮点对照可用
`python tools/build.py --decoder-float --output-dir build-float`，或 CMake
`-DAPTX_AA_DECODER_FIXED_POINT=OFF`。详见 [解码器设计](docs/zh-CN/decode.md)。

可选零CRT库构建：`python tools/build.py --no-crt --output-dir build-nocrt`，
或CMake `-DAPTX_AA_NO_CRT=ON`。复用现有公共 `audio_math` 整数契约，实例需显式
传入宿主分配器；普通SDK默认不变。已实测MSBuild Win32/x64及Android ARM32/ARM64，
不使用`-fno-builtin`或`-ffreestanding`，详见 [构建选项](docs/zh-CN/build-options.md)。

`.sys`宿主可用`--driver-resources`或CMake `APTX_AA_DRIVER_RESOURCES=ON`选择固定工作区、
无处理期分配及较低栈用量；非分页池适配。

零CRT热点已消除共用math小位型复制的函数调用，并按int32样本移动历史缓冲；
同样本简测已接近CRT。

生成后的数值表已提交到源码树。项目保留上游 MIT 许可及来源说明，见 `LICENSES/AKS-Logic_LICENSES`和 `UPSTREAM_LEGAL.md`。
