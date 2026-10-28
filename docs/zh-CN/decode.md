# aptX Adaptive 解码器

解码算法已加入同一 C11 库，公开入口在 `include/aptx_aa.h`，可执行程序为
`build/aptx_aa_decode.exe`。支持上游编码器生成的双声道 config 0/1/3/4，包括
R1/R2 音乐模式。不是把有损编码还原成原始 PCM；量化丢失的信息不能恢复。

解码实现现位于 `aptx/decoder`，独立公开头为
`aptx/decoder/include/aptx/decoder/decoder.h`，可单独链接 `aptx_aa::decoder`。
`src/api` 管理句柄与跨包缓冲，`src/codec` 负责位流及整数重建，`src/dsp` 负责
合成滤波；公共预测器与数值表依赖 `aptx/common`。原聚合头保持可用。

实际 aptX 码流处理依据本项目编码器、系数表，以及上游测试专用的
`crates/aptx-adaptive/src/codec/decode.rs`。

## 解码流程

```text
codec packet（自动识别 raw / byte swap）
  → 校验包头、长度和 padding
  → exp-Golomb carry 长度与跨包拼接
  → 绝对/差分比特分配 Huffman 解码
  → 子带量化码 Huffman 解码及原始扩展位
  → 有符号量化码、反量化、步长更新
  → 加入当前预测值，更新预测器和抖动状态
  → 子带合成 FIR、overlap-add
  → 交错双声道、24 位有效数值 PCM
```

每次 Huffman composer 符号解出两个子带块。读取比特分配后，每个普通量化帧最多
64 块；config 3 的全零分配有独立的块数逃逸字段。高于 9 bit 的量化码和分配
逃逸按编码端的表反向处理；4-band 最高子带的 2-bit composer 使用专门的表。

编码器把前一个逻辑包的溢出字节放在下一物理包开头。解码器因此缓存一个包，
收到下一个包时补齐前一个包再输出。这避免把跨包的半个量化码错误地当成新帧。
收尾时只解出完整的 code pair，不能凭空补回最后未传输的 carry。

## 反量化与预测

先将码流字段符号扩展为 `q`。令 `e=max(bits-9,0)`、`c=q>>e`，按符号确定
反量化表索引 `j`。结合阈值表、扩展低位、当前步长和抖动，计算重建残差 `r`。
步长自适应及其饱和、溢出、舍入规则与编码器本地重建保持相同。

合成的子带值为 `sat24(prediction + r)`，预测器则使用 `r` 更新；不能用输出
PCM 或完整子带值代替残差更新。抖动种子由当前块前两个量化码生成，供后续块
使用。config 1 按上游低功耗路径处理，包括它的最高子带历史重建行为。

这部分已经与独立参考捕获逐项比较：量化码、分配、重建残差、更新后的预测值
完全一致；同时检查了重建子带等于饱和后的参考 `pred_in + quant_recon`。

## 合成滤波器推导

编码器分析端可写作子带抽取 FIR。去掉定点舍入、溢出及饱和后，在正常幅度的
线性范围内，分析滤波器组近似满足常数增益的 paraunitary 条件。使用时间反转的
转置滤波器并校正增益，得到因果合成滤波器：

- 4-band QMF 树：合并两级分析 FIR，转置增益校正为 8。
- 8-band CMFB：展开前端和 DCT，8×16 调制矩阵满足 `T·Tᵀ ≈ 8I`，完整滤波器
  的转置增益校正为 64。

条件编译的只读 `double` / Q29 FIR 表已预生成并提交到源码树，构建不需要外部软件包。
运行时进行子带加权和、时间反向索引及环形 overlap-add，最后舍入并
饱和到 24 位 PCM。当前没有 SIMD 优化。

| config | 合并 FIR 长度 | 分析＋合成延迟（每声道样本） |
|---|---:|---:|
| 0 | 94 | 90 |
| 1 | 70 | 66 |
| 3 | 256 | 248 |
| 4 | 128 | 120 |

这里的延迟由本实现实测，不包含包缓存、周期等待或蓝牙传输。上游其他接口中的
延迟元数据用途不同，不应混用。文件工具默认裁去这段开头延迟，并在结尾输出
合成历史；库 API 保留原始时间轴，由调用者自行处理。

单独把参考 `analysis_out` 送入合成器，所得 PCM 在对齐后重建 SNR 为约
92–113 dB；另测试了 config 4 和 88.2 kHz。**这不是最终有损码流的音质指标**。
最终码流在低码率会主动省略频带，恢复 PCM 与原始输入不可能逐样本相同。

合成器是从编码端推导出的实现，现有浮点和定点两条路径，尚未与厂商解码器 PCM 做独立对照。即便
整数重建状态精确一致，也不能因此声称最终 PCM 与厂商解码器 bit-exact。

## 命令行

```powershell
# 自动检测采样率、编码 config、包长及 byte swap，默认输出 PCM24 WAV
.\build\aptx_aa_decode.exe -i output.aptxaa -o decoded.wav

# PCM16 WAV
.\build\aptx_aa_decode.exe -i output.aptxaa -o decoded16.wav --bits 16

# 右对齐 24 位、符号扩展、小端 int32 槽，与编码器 --raw s32le24 对应
.\build\aptx_aa_decode.exe -i output.aptxaa -o decoded.s32le --raw

# 保留分析/合成延迟
.\build\aptx_aa_decode.exe -i output.aptxaa -o delayed.wav --keep-delay
```

输入必须从第一个 codec 包开始。WAV32 写完整 32 位范围，即 PCM24 左移 8 位；
`--raw` 则保持右对齐，不使用这个左移。RIFF WAV 文件大小限定在 4 GiB 内。

## C 接口

```c
aptx_aa_config config = {48000, 2, APTX_AA_HQ, APTX_AA_R2};
aptx_aa_dec *decoder = aptx_aa_decoder_create(&config);
/* 也可以用 create_variant(0/1/3/4, sample_rate)。构造失败返回 NULL。 */

size_t samples = 0;
int rc = aptx_aa_decoder_push(decoder, packet, packet_bytes,
                               pcm, pcm_capacity, &samples);
/* 成功时接收整个 packet，输出 samples 个交错样本；首包输出 0。
 * OUTPUT_TOO_SMALL 时 samples 是所需容量，不推进状态；扩大 PCM 缓冲后
 * 使用相同 packet 重试。可用 pcm=NULL, capacity=0 查询所需容量。
 */

rc = aptx_aa_decoder_finish(decoder, pcm, pcm_capacity, &samples);
/* 同样支持 OUTPUT_TOO_SMALL 重试。输出最后完整量化码和合成尾部。
 * 再次 finish 输出 0；后续新流先 reset，或重新创建匹配配置的实例。
 */
aptx_aa_decoder_destroy(decoder);
```

输出容量单位是 `int32_t` 样本数，不是字节数或双声道帧数。上面是调用顺序示意，
实际应用需要检查构造和每次调用结果，并提供输入输出存储。可参照
`tools/cli/aptx_aa_decode.c` 中的扩容重试逻辑。

反量化和预测状态使用整数，PCM 合成可在编译期选择浮点或定点。解码实例互相独立，同一个实例
需串行调用。正常 push/finish 不分配堆内存，实例创建时准备状态和 Huffman 树。

## 限制与验证

- 支持 stereo config 0/1/3/4；语音、R3/R4、Lossless、其他 channel mode 不支持。
- 不解析 RTP/A2DP/SPF 容器，不提供重采样、丢包隐藏或自动追踪不连续流。
- 能恢复跨一个后续物理包的 carry。异常小包导致 carry 需要横跨多个后续包时，
  返回 `APTX_AA_UNSUPPORTED_STREAM`，不伪造 PCM。
- 从中途接入或丢包后，仅重置状态不能恢复已经缺失的预测历史；应重新建立流。
- 数据没有 CRC 保证。语法检查可拒绝一部分损坏输入，不能证明通过检查的数据
  从未被篡改或丢失。损坏包可能需要到下一次 push/finish 才被识别。
- EOF 可缺少最后一个 code pair 的一部分；finish 丢弃它。物理包截断则由文件
  工具报错。不同输入时长下，最后可恢复的样本数不必等于源 PCM 长度。

运行：

```powershell
python aptx/decoder/tests/validate_decoder.py build/aptx_aa.dll
python aptx/decoder/tests/validate_decoder_cli.py build/aptx_aa_decode.exe
```

## 编译选择定点解码

`APTX_AA_DECODER_FIXED_POINT=1` 为默认定点合成，整个解码运算使用整数；
设为 `0` 切回浮点，公开API、PCM格式、延迟和流状态行为保持相同。

```powershell
# 直接构建：两个版本放在不同目录
python tools/build.py --decoder-float --output-dir build-float
python tools/build.py --decoder-fixed-point --output-dir build-fixed

# CMake：OFF为浮点，ON为定点（默认）
cmake -S . -B build-fixed-cmake -DAPTX_AA_DECODER_FIXED_POINT=ON
cmake --build build-fixed-cmake --config Release
```

已有CMake构建目录会保留缓存中的旧选项；切换为定点时显式传入
`-DAPTX_AA_DECODER_FIXED_POINT=ON`，浮点对照则传入 `OFF`。

自行编译时，将 `-DAPTX_AA_DECODER_FIXED_POINT=1` 应用到全部解码器编译单元，
包括 `synthesis_tables.c`。切换宏后须完整重编解码库，不能混合两种系数类型的对象文件。
可调用 `aptx_aa_decoder_fixed_point()` 查询实际库的编译模式，不依赖调用方的头文件宏值。

定点路径使用32位有符号系数、29位小数，64位乘加与 overlap 状态。
所有贡献累加完成后才舍入，半整数向正无穷舍入，最后饱和到24位，匹配原浮点路径的规则。
各相位系数绝对值总和给出保守上界，生成表内的静态断言验证：

```text
8388608 * max_phase_sum(abs(Q29_coefficients)) + 2^28 < INT64_MAX
PCM = sat24((accumulator + 2^28) >> 29)
```

输入子带由反量化/预测重建饱和到24位，该范围是上述溢出证明的前提。
config 0/1/4 相对浮点路径的理论舍入差上界为1个24位PCM LSB，config 3为2个LSB。
这是一份既有合成滤波器的定点实现，不能据此宣称复现了厂商定点滤波器或最终PCM。
定点选择及驱动构建配置见[构建选项](build-options.md)。

## 可选零CRT库

`APTX_AA_NO_CRT=1`（默认0）选择外部宿主分配和现有公共math整数辅助实现。
此时使用 `aptx_aa_decoder_create_with_allocator()` 或对应variant入口，仍以原destroy
释放；原构造函数因默认堆不可用返回NULL。新分配回调契约、编译选项和依赖验收见
[构建选项](build-options.md)说明分配器回调约定和零CRT配置。解析临时区现放在解码实例中，增加24,768字节实例空间，
减少push调用栈；不改变PCM或容量重试行为。

后续驱动资源优化已把bit缓冲压缩、Huffman树共享，并新增caller-owned初始化；
当前x64解码工作区为21608字节。使用查询接口取得实际大小。
