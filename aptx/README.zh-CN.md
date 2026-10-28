# aptX Adaptive 编解码组件

本目录包含发布版使用的编解码组件：

- [`common`](common)：公共类型、只读表、预测器和运行时适配；
- [`encoder`](encoder)：编码核心与带队列的流控制；
- [`decoder`](decoder)：包解析、信号重建和合成；
- [`platform`](platform)：不属于可移植核心的平台专用适配。

顶层 CMake 选项允许独立选择组件。可变状态均属于实例，初始化成功后的处理路径不再
进行动态内存分配。

