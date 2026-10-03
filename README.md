# ttp_flac

供千千静听原版与 TTPlayer Rebuild 使用的独立 x86 **FLAC / TTA 输入插件**。

根据原 `AddIn/ttp_flac.dll` 的伪代码、二进制接口和实际输出重建，恢复两个 Reader 工厂。FLAC 使用固定版本 **libFLAC 1.5.0**；TTA 的 Rice 解码、预测、滤波、声道还原及历史浮点分支在本项目中实现。

这是可维护的兼容实现，不是原二进制逐字节复制，也不宣称恢复了原始源代码。原版已确认的越界写入、错误返回和标签保存缺陷已修正；具体差异见[实现及验证说明](docs/IMPLEMENTATION_AND_VALIDATION.md)。

## 功能

- 原 `ttpGetSoundAddIn`、ReaderCreator、Reader、Metadata、Thumbnail x86 ABI。
- `FLAC Reader`：`*.flac;*.fla`；`TTA Reader`：`*.tta`。
- 宿主 IStream、扩展读取和回调转发，毫秒定位、时长、格式及码率查询。
- FLAC 8/16/24/32 位整数 PCM；TTA 8/16/24 位整数及历史 format 3 的 32 位浮点输出已验证。
- FLAC 原生 UTF-8 标签、重复字段、用户字段和 ReplayGain；TTA 标签转交宿主 `CreateStdContent(type=4)`。
- FLAC 多张 PICTURE 封面，新增 PNG/JPEG/GIF、替换、删除；正确写入类型、尺寸、色深。
- FLAC 修改在最终 Release 时保存；提供可选 Commit 接口向新宿主返回真实保存结果，旧接口兼容。压缩音频不重编码；Unicode 路径、并发文件变化检查、写失败回滚。
- 同一个 DLL 用于 XP SP3、Win7 和新系统，CPU 需要 SSE2，无需另装 VC/UCRT 运行库。

原 DLL 也没有 Encoder 或独立 Decoder 工厂，本项目保持这一结构。TTA 的通用标签依赖宿主，不在 TTA Reader 上增加原版没有的 Thumbnail 接口。

## 构建

需要 Windows、现代 MSVC x86 C++ 工具、Windows SDK、CMake 3.24+、PowerShell 和 Python 3。默认使用 VS 2026；安装其他版本时传入对应生成器。

```powershell
./build.ps1
./build.ps1 -Package
./build.ps1 -Package -PackageVersion 2026.10.04
# 示例：VS 2022
./build.ps1 -Generator 'Visual Studio 17 2022' -Package
```

构建自动下载并核对固定 SHA-256 的 libFLAC、VC-LTL 5.3.1、YY-Thunks 1.2.2。上游源码只进入忽略的构建目录，仓库保留版权及许可证。无需 v141_xp 或 VS2019 Build Tools。

默认版本为北京时间 `yyyy.MM.dd`；Action 的 **Release a Version** 在同日已有版本时分配 `p1`、`p2` 等，编译前即写入 DLL 的文件版本和产品版本。

输出：

```text
build/Release/ttp_flac.dll
build/Release/ttp_flac-yyyy.MM.dd[pN].zip
build/Release/SHA256SUMS.txt
```

ZIP 只含 `AddIn/ttp_flac.dll` 和 `SHA256SUMS.txt`。关闭播放器后，将 DLL 放入其 `AddIn` 目录。该插件与 `ttp_aac`、`ttp_ogg`、`rebuild` 分别构建；这里的工作流以 `ttp_flac` 为独立仓库根目录。

## 验证与范围

已在 Windows 11 本机及 XP、Win7 虚拟机验证原版与重建版加载、FLAC/TTA 播放，以及独立接口、标签和封面检查。Windows 11 按用户要求代替 Windows 10 验证；没有把它记为真实 Windows 10 测试。

测试和原 DLL 分析材料位于工作区的 `rebuild/tests/flac_rebuild`、`rebuild/tests/flac_analysis`，不属于此插件源码仓库，不上传、不打包，Action 不运行这些测试。

支持 native FLAC/TTA 和可定位流。Ogg FLAC、MP4 内 FLAC、TTA2、加密 TTA、不可定位网络流不在本次恢复范围。外部 ID3 特殊编码的写入、保存错误反馈及极长路径限制详见[验证说明](docs/IMPLEMENTATION_AND_VALIDATION.md#已知限制)。

## 文档与许可证

- [原 DLL 伪代码与二进制分析](docs/ORIGINAL_FLAC_PLUGIN_ANALYSIS.md)
- [实现、原版差异及验证结果](docs/IMPLEMENTATION_AND_VALIDATION.md)
- [可选元数据提交接口与宿主接入](docs/COMMIT_INTERFACE.md)
- [依赖来源与固定摘要](third_party/ORIGIN.md)
- 本项目代码：[MIT](LICENSE)。TTA 参考算法另外适用[原 BSD 声明](docs/licenses/TTA-BSD.txt)。
- [libFLAC](third_party/libFLAC/COPYING.Xiph)、[VC-LTL](docs/licenses/VC-LTL-LICENSE.txt)、[YY-Thunks](docs/licenses/YY-Thunks-LICENSE.txt)。

许可证保留在仓库，不作为 DLL 资源嵌入，也不放入运行时 ZIP。发布说明链接到对应提交的版权和完整许可证，作为发行附带材料。
