# TTA 32 位边界复核（2026-10-24）

## 结论

本轮补充历史 `format=3` 浮点 TTA 的位模式和跨帧定位验证，没有发现重建解码器与原 DLL 的输出差异，因此没有修改生产解码逻辑或更换 TTA 库。

整数 `format=1, bits=32` 不能因为读取头成功就标为已完整支持。TTA 官方仍将整数编码能力列为 8、16、24 位；FFmpeg 的 TTA 编码器接收 `S32` 容器时把实际编码精度设为 24 位。因此，用 FFmpeg 从 32-bit WAV 转出的 TTA 不能用作原生 32-bit 整数验收样本。本轮保留现有兼容分支，不定义或扩展私有 TTA 格式，也不将其列入已验证的完整整数精度范围。

依据：[TTA 官方说明](https://tta.sourceforge.net/en/true-audio-codec-tta/)、[TTA 帧与定位表说明](https://tta.sourceforge.net/en/tta-format-description/)、[FFmpeg 官方 ttaenc.c](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/ttaenc.c)。工作区固定 FFmpeg 9.0.2 源码的 `tta_encode_init` 也具有相同的 24 位限制。

## 新增验证

本地生成 8 组 `format=3 / float32 / 8 kHz` 样本，每组 10017 个采样帧，覆盖 1、2、6、8 声道：

- 一组重复特殊 IEEE 754 位模式：正负零、最小和最大次正规数、最小正规数、正负 1、最大有限数、正负无穷、quiet/signaling NaN 及 payload。
- 一组固定随机种子的原始 32 位模式；比较原始字节，不对 NaN 做浮点数值比较或规范化。
- 原 DLL、重建 DLL 全量解码均与预期 PCM 逐字节相同。
- 重建 DLL 在 0、551、1044、1045、1111 ms 定位后，与源 PCM 对应位置逐字节比较。1044/1045 ms 位于 8359 采样帧的 TTA 帧边界两侧。

Windows 11 本机：56 次探针调用与 56 次字节核对，共 112 项通过。便携包本地再次运行得到 `CASES=56 FAILURES=0`。这些是本轮新增结果；XP/Win7 是否重跑本包由综合验收记录单独列出，不能从前一轮测试自动推定。

历史浮点中，解码后的内部 `(high=0, |low|=1)` 表示正负零。生成器必须避开将正负 1 同样编码为这个保留组合；本轮为该测试值使用解码器接受、低 16 位相同的非零 high 值。原 DLL 与重建 DLL 在此规则上也完全一致，不能把错误测试编码造成的正负 1 变零误判为生产解码器缺陷。

## 证据及限制

- 原 DLL SHA-256：`e2833c023377787fa9705d52215d3a02b7f5a742bdda4012f9d639c2e1f64dbd`。
- 本轮重建 DLL SHA-256：`21c799147723a50283e70676a99f299e018a1744f390556f75dae819a307dd69`，与本轮开始时相同。
- 本地脚本、样本、PCM、日志和便携包：工作区 `rebuild/tests/plugins_followup_20261024/tta`。不加入插件仓库、Action 或发行包。
- 原算法依据继续使用已记录的 `60304381`、`60304494`、`60304C38`，见[原版分析](ORIGINAL_FLAC_PLUGIN_ANALYSIS.md#6-容易遗漏的-tta)。

本轮样本是本地测试编码器生成，再由独立原 DLL 和重建 DLL 核对，不是第三方历史编码器生成的真实存档。获取历史参考编码器的下载未取得可用归档，未把返回的 HTML 当成源码或运行文件。**历史真实 float32 存档与独立原生 int32 编码器样本仍未覆盖。** 这些测试也没有验证原版宿主或音频设备是否会接收、播放 NaN/Inf；此类值只用于无声的解码接口边界检查。

## 三系统最终验收

同一 `2026.10.24` DLL 在 XP SP3、Win7 SP1 与 Windows 11 的平铺矩阵均为 `CASES=56 FAILURES=0`。每次解码／定位另以 `fc /b` 比较期望 PCM；本轮没有修改 FLAC／TTA 生产代码，也不另生成同内容的 p1 DLL。
