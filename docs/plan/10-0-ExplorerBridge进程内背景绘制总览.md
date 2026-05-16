# 10-0 Explorer Bridge 进程内背景绘制总览

## 目标

建立新的真实背景绘制主线：

`让 Fence 背景绘制发生在 Explorer 进程内，而不是通过外部窗口压层级。`

## 为什么需要 Bridge

Explorer 的桌面图标由 `SysListView32` 绘制。外部窗口无法可靠插入到“ListView 背景”和“图标绘制”之间。

Bridge DLL 进入 Explorer 后，可以在同一进程内：

- 持有背景 bitmap 资源生命周期。
- 调用 ListView 背景相关消息。
- 必要时 subclass `SysListView32` 或 `SHELLDLL_DefView`。
- 在 Explorer 自己的绘制流程中画 Fence 背景。

## 分阶段

1. `10-1` Bridge DLL 注入与生命周期。
2. `10-2` Bridge IPC 与 Fence 数据同步。
3. `10-3` Explorer 内 ListView 背景绘制 PoC。
4. `10-4` Explorer 内绘制稳定化与回退。

## 成功标准

- DLL 能进入 Explorer 并写独立日志。
- DLL 能定位桌面 `SysListView32`。
- 主程序能把 Fence rects 发送给 DLL。
- 背景能显示在图标下方。
- 退出后背景清理干净。
