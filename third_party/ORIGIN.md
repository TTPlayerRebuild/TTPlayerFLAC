# 第三方来源

记录日期：2026-10-03。源码仓库不收录下载的依赖源码或编译产物。

| 组件 | 固定版本 | 用途 | 本地声明 |
| --- | --- | --- | --- |
| libFLAC | 1.5.0 | 静态链接的 native FLAC 解码器 | [COPYING.Xiph](libFLAC/COPYING.Xiph) |
| VC-LTL | 5.3.1 | 使用旧系统自带 CRT，避免额外 VC/UCRT 安装 | [许可证](../docs/licenses/VC-LTL-LICENSE.txt) |
| YY-Thunks | 1.2.2 | 新工具链的 XP API 兼容 | [许可证](../docs/licenses/YY-Thunks-LICENSE.txt) |

## 构建下载

`cmake/flac.cmake`、`cmake/legacy_windows.cmake` 固定以下地址和 SHA-256，摘要不符即停止：

```text
https://downloads.xiph.org/releases/flac/flac-1.5.0.tar.xz
f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920

https://github.com/Chuyu-Team/VC-LTL5/releases/download/v5.3.1/VC-LTL-Binary.7z
7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad

https://github.com/Chuyu-Team/YY-Thunks/releases/download/v1.2.2/YY-Thunks-Objs.zip
518ed7ef4825e8a41997fbccfa2c8090cf31a6038fd51520a2e49886f947f9fc
```

FLAC 仓库的工具、编码程序、文档和其他许可证组件不作为插件运行时发布。仅链接所需的 BSD libFLAC；关闭 Ogg、AVX、线程、示例、测试、C++ 包装库和命令行程序。CPU 最低 SSE2。

官方说明：[FLAC 下载](https://www.xiph.org/downloads/)、[更新记录](https://www.xiph.org/flac/changelog.html)、[许可证](https://www.xiph.org/flac/license.html)。

## TTA 算法

主要恢复依据为原 DLL 的 `60303C5A`、`60304381`、`60304494`、`60304C38` 等函数及二进制常量。整数 Rice／滤波路径交叉参考 True Audio Software 的 BSD 解码器，作者 Alexander Djourik、Pavel Zhilin，Copyright (c) 2004。

分析时参考文件保留于私有测试目录，不参与插件构建：

```text
https://raw.githubusercontent.com/rockbox/rockbox/master/lib/rbcodec/codecs/libtta/ttadec.c
SHA256 6e64269da521c533e271b9f8174e72f3e5b3c4b0fcdf5df26c1ca2e0fef91e95

https://raw.githubusercontent.com/rockbox/rockbox/master/lib/rbcodec/codecs/libtta/filter.h
SHA256 b4f8c487c7085a4e948a9e2d3db945bf33c29624bbed9df3453092057e8bb6d7
```

上述 master 地址用于说明参考来源，文件摘要标识实际阅读版本；构建不会下载或依赖这两个地址。完整原版权、三条分发条件及免责声明保存在 [TTA-BSD.txt](../docs/licenses/TTA-BSD.txt)，TTA 源文件也指向该声明。

历史浮点还原分支根据原版逻辑重建，并用单／双／六声道样本与原 DLL 的 PCM 逐字节核对。
