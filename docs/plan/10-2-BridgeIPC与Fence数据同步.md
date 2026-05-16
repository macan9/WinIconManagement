# 10-2 Bridge IPC 与 Fence 数据同步

## 目标

让主程序可以把 Fence 几何和样式数据发送到 Explorer 内的 Bridge。

## IPC 要求

第一版只需要支持：

- `HELLO`
- `SET_FENCES`
- `REFRESH`
- `CLEAR`
- `SHUTDOWN`

数据内容：

- Fence id
- bounds
- active state
- fill color
- border color
- corner radius
- opacity

## 执行步骤

1. 选择 IPC 方式，优先命名管道或 `WM_COPYDATA` 到 Bridge 消息窗口。
2. 定义稳定的消息结构和版本号。
3. 主程序在 Fence 列表变化时发送 `SET_FENCES`。
4. Bridge 收到数据后只缓存，不立即做复杂绘制。
5. Bridge 写日志确认收到数量、rect 和版本。
6. 主程序处理 Bridge 不存在、Explorer 重启、发送失败。

## 验收标准

- 单 Fence bounds 能进入 Bridge 日志。
- 多 Fence 数据结构能传输。
- Explorer 重启后 IPC 自动失效并重连。
- 发送失败不会影响图标移动和应用退出。
