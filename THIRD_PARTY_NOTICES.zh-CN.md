# 第三方引用说明

## AKS-Logic aptx-adaptive-linux

本项目参考
[AKS-Logic/aptx-adaptive-linux](https://github.com/AKS-Logic/aptx-adaptive-linux)
进行 C 语言改写，主要参考 `crates/aptx-adaptive` 在版本
`f8a090875ae2d23721f0ed175f77228f190444f1` 中的实现。

所参考的上游项目版权归 Copyright (c) 2026 Prateek Mittal（AKS-Logic）所有，
采用 MIT 许可证。必须保留的许可证原文位于
[`LICENSES/AKS-Logic_LICENSES`](LICENSES/AKS-Logic_LICENSES)，仓库根目录的
[`LICENSE`](LICENSE) 也保留了相同文本。

本项目根据上游实现改写了编码算法、模式数据、系数和编码表的 C 版本，并自主编写了解码
算法，并针对本仓库重新组织公开 C 接口、构建结构、宿主内存分配、驱动资源模式和本地验
证工具。项目不包含或加载厂商二进制文件。

上游的 clean-room 与法律声明保留在 [`UPSTREAM_LEGAL.md`](UPSTREAM_LEGAL.md)。
`aptX` 及相关名称是 Qualcomm Incorporated 的商标；本项目与 Qualcomm 不存在隶属、
赞助或背书关系。
