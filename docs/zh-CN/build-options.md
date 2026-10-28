# 编译选项与 LHDC/SYS 合并

aptX Adaptive 的选项按 LHDC 工程的组件裁剪和资源配置方式组织。作为独立顶层工程时，
编码器、解码器、组合库和 CLI 默认开启；测试默认关闭。作为 LHDC 超级工程的子目录时，
组件、组合库、测试、驱动资源和仅外部分配的默认值会跟随同名 `LHDC_*` 选项。

| CMake 选项 | 独立默认 | 含义 |
|---|---:|---|
| `APTX_AA_BUILD_ENCODER` | `ON` | 构建 `aptx_aa::encoder` |
| `APTX_AA_BUILD_DECODER` | `ON` | 构建 `aptx_aa::decoder` |
| `APTX_AA_BUILD_BUNDLE` | `ON` | 用已选择的组件构建 `aptx_aa::codec` |
| `APTX_AA_BUILD_TOOLS` | 顶层 `ON`，子工程 `OFF` | 构建编码、解码 CLI |
| `APTX_AA_BUILD_TESTS` | `OFF` | 构建并注册 CTest |
| `APTX_AA_EXTERNAL_ALLOCATOR_ONLY` | `OFF` | 禁止默认 CRT 堆，只允许显式 allocator 或工作区 |
| `APTX_AA_NO_CRT` | 跟随驱动资源 | 使用无 CRT 编译和链接契约 |
| `APTX_AA_DRIVER_RESOURCES` | `OFF` | 组合选择无 CRT、仅外部分配、定点解码和有界资源路径 |
| `APTX_AA_DECODER_FIXED_POINT` | `ON` | 使用定点解码合成 |
| `AUDIO_COMMON_ROOT` | 相邻的 `../audio_codec_common` | 指向双方共用的 core 与 math 源码树 |

`APTX_AA_EXTERNAL_ALLOCATOR_ONLY` 与 `APTX_AA_NO_CRT` 分离：前者只关闭默认堆，仍可由
最终 SYS 使用经过选择的 CRT；后者还启用 freestanding 编译约束。`DRIVER_RESOURCES=ON`
要求 `NO_CRT=ON`、`EXTERNAL_ALLOCATOR_ONLY=ON`、定点解码和 Release/NDEBUG。

## 同一 SYS 的建议配置

和 LHDC release 一样，aptX 源码直接使用标准 `memcpy`、`memset`、`memmove`、`memcmp`。
库内不定义私有字节循环或同名 C 运行时符号；编译器优先生成 intrinsic/本地实现，仍保留
的外部辅助符号由最终 SYS 的正常工具链链接统一解析：

```cmake
set(LHDC_DRIVER_RESOURCES ON CACHE BOOL "" FORCE)
set(LHDC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LHDC_BUILD_BUNDLE OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/lhdc_codes lhdc_build)

set(AUDIO_COMMON_ROOT "path/to/audio_codec_common" CACHE PATH "" FORCE)
set(AUDIO_COMMON_NO_CRT ON CACHE BOOL "" FORCE)
set(AUDIO_COMMON_DRIVER_RESOURCES ON CACHE BOOL "" FORCE)
add_subdirectory("${AUDIO_COMMON_ROOT}" audio_common_build)
set(APTX_AA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(APTX_AA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APTX_AA_BUILD_BUNDLE OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/aptx_adaptive_codes aptx_build)

target_link_libraries(my_driver_objects PRIVATE lhdc::encoder aptx_aa::encoder)
```

上面的顺序兼容尚未移除内置 `math` 的 LHDC 开发树：公共仓库会复用已经存在的
`audio_math` target。LHDC 迁移完成后，最终超级工程应把 `audio_codec_common` 放在两个
codec 之前，此时 math 与 core 都只由公共仓库定义一次。

只需要发送端编码时，可同时设置 `LHDC_BUILD_DECODER=OFF` 和
`APTX_AA_BUILD_DECODER=OFF`；只需要接收端时则关闭两个编码器选项。组件静态目标会传递
`aptx_aa::common` 依赖，不需要手工重复列出公共库。

## 非 CMake 直接构建

`tools/build.py` 提供对应参数：`--no-encoder`、`--no-decoder`、`--no-bundle`、
`--no-tools` 和 `--external-allocator-only`。直接构建与 CMake 构建一样，不会定义私有
`memcpy`/`memset` 等运行时符号。

## Visual Studio/MSBuild

`aptx_adaptive_windows.sln` 使用 Visual Studio 2019 和 v142 工具集提供普通宿主 Win32/x64
构建。解决方案要求 `audio_codec_common` 与本仓库处于同级目录，包含三个组件静态库、组合
DLL 和两个 CLI 工程。

```powershell
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=x64
msbuild aptx_adaptive_windows.sln /m /p:Configuration=Release /p:Platform=Win32
```

`AptxAaDecoderFixedPoint` 默认值为 `1`，浮点解码对照可设为 `0`。这些仓库内置工程明确面向
普通 CRT 宿主配置；零 CRT 或 SYS 集成应使用前述 CMake 选项和最终 WDK 宿主工程。
