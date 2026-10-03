# 可选元数据提交接口

日期：2026-10-04；首次提供版本：`2026.10.04`。

## 原版兼容

原 Metadata 只有 Count、At、Get、Set。FLAC 的 Set 修改内存，真正写盘发生在 Reader 最后一次 Release；Release 的返回值是引用计数，无法报告保存结果。

新增接口通过独立 QueryInterface 获取，不增加原 Reader、Metadata、Thumbnail 或工厂的虚表槽，也不增加 DLL 导出。原版播放器继续最终 Release 保存。TTA 标签委托宿主通用标签实现，TTA Reader 对新 IID 返回 `E_NOINTERFACE`。

## ABI

声明位于 [ttp_flac_abi.h](../include/ttp_flac_abi.h)。x86，使用 `STDMETHODCALLTYPE`：

```cpp
// {3AB643C1-D8A4-4D49-9CBB-EF196B04B6E7}
struct MetadataCommit : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Commit() = 0;
};
```

槽 0、1、2 是 QueryInterface、AddRef、Release，槽 3 是 Commit。QI 成功增加引用计数；从任意接口查询 IID_IUnknown 都返回同一 Reader 身份。调用方必须释放取得的接口。

## 一次提交结束一个 Reader

1. 为编辑单独打开 Reader，设置标签和封面。
2. 查询新接口；若为 `E_NOINTERFACE`，沿用释放全部引用后重新读取验证的旧流程。
3. Commit 结束解码、释放内部通用标签对象，执行标签与封面的同一次文件事务。
4. 成功或失败后，此 Reader 均不能继续 Open、Read、Seek 或修改。应释放所有引用。
5. 重复 Commit 返回第一次的 HRESULT，不再写盘；最终 Release 也不重试显式提交，避免界面已报错后悄悄更改文件。
6. 用户重试时，应重新打开 Reader、重新应用保留的草稿，再提交。

只读缓存仍可查询，但不能将其当成磁盘验证结果。成功后建议释放旧 Reader，再打开文件核对标签和封面。不得对正在播放的共用 Reader 调用这个终结式接口，应使用独立的写入会话，或继续旧的最终 Release 行为。

## 返回值与保存

| 结果 | 含义 |
| --- | --- |
| `S_OK` | 保存完成，或没有待保存修改 |
| `E_UNEXPECTED` | 尚未成功 Open，或提交后继续读、定位、修改 |
| `E_ACCESSDENIED` | 文件只读等禁止写入情况 |
| `HRESULT_FROM_WIN32(ERROR_FILE_INVALID)` | 文件长度或最后写入时间已改变 |
| `HRESULT_FROM_WIN32(ERROR_DISK_FULL)` | Win32 文件操作报告磁盘已满 |
| `STG_E_WRITEFAULT`、`STG_E_MEDIUMFULL` 等 | 原始流／文件操作失败，保留真实 HRESULT |

具体错误因失败位置和宿主流实现而异，不应只识别这些值。Set 成功仅表示接受编辑，不表示落盘。

保存先生成完整临时文件并刷新，再尝试 ReplaceFileW。宿主句柄阻止替换时，才创建完整备份并通过原文件流写回；短写、刷新失败等触发回滚。回滚也失败时保留恢复文件并输出调试路径。流回写不是断电原子操作，不能在所有异常环境下保证原文件已经恢复。

## 重建宿主

LegacyReaderSession::CommitMetadata() 查询新 IID 并保护跨 DLL 调用。不支持接口时返回 `S_FALSE`，意思是需要走旧流程，不表示已经保存。支持时缓存结果，并禁止会话继续读写。

文件属性写入进程在标签／封面设置完毕、Reader 释放之前调用它。失败结果回传界面，显示系统描述及十六进制 HRESULT，保留编辑。随后仍执行释放和重新读取验证，兼容旧插件。

增益扫描写入、增益删除、使用独立 Reader 的内嵌歌词保存也接入。正在播放的共用 Reader 的歌词写入保留旧生命周期，不强行结束播放。

## 验证

- 显式成功、短写、流刷新失败、只读、外部文件变化；失败回滚、重复调用以及 Release 不重试。
- 原版文件属性窗口实际修改标题并保存，确认最终 Release 路径落盘。
- XP SP3、Win7 SP1、Windows 11 的真实重建版文件属性代码和写入子进程：新 DLL 成功、无新接口的上一版 DLL 回退、测试 DLL 模拟磁盘已满。
- 磁盘已满错误 `0x80070070` 到达界面，草稿和保存按钮状态保留。

测试代码和注入器仅在工作区 rebuild/tests，不进入插件、运行时包或 Action。
