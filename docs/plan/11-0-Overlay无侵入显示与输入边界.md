# 11-0 Overlay 无侵入显示与输入边界

## 目标

实现一个不扰动桌面原生交互的 Overlay 基础层。

Overlay 是前景提示和小范围交互层，不是真实背景层。

## 是否对接 Explorer 内部

Overlay 不对接 Explorer 进程内部。

它只消费主程序运行时已经整理好的桌面坐标、Fence 几何、活动态和热区数据。Explorer 内部窗口、`SysListView32` 绘制链路、背景 bitmap、subclass 或 `LVM_SETBKIMAGEW` 都属于 `10` 的 Explorer Bridge 范围。

换句话说：

- Overlay 只在 Explorer 外部做前景提示。
- Overlay 不注入 Explorer。
- Overlay 不 subclass Explorer 窗口。
- Overlay 不向 `SysListView32` 发送绘制相关消息。
- Overlay 不参与真实 Fence 背景绘制。

## 职责

Overlay 可以负责：

- 框选中的临时矩形。
- 创建确认 UI。
- 活动 Fence 的边框。
- 标题和轻量状态提示。
- 删除按钮和缩放手柄。

Overlay 不负责：

- 真实 Fence 背景填充。
- 图标下方背景。
- 全屏输入接管。
- 替代 Explorer 桌面。

## 输入边界

- 默认 click-through。
- 只有确认 UI、删除按钮、缩放手柄、移动热区等小范围区域可以接管输入。
- 空状态下不影响桌面右键。
- 不影响任务栏、托盘、普通窗口。

## 层级边界

- Overlay 不能通过异常 Explorer host 抬升为全屏遮罩。
- Overlay 不能依赖全屏透明窗口承担背景。
- Overlay 的显示问题不能通过强行调整 WorkerW/Progman 解决。

## 执行步骤

1. 创建 `OverlayWindow`。
2. 默认设置 click-through。
3. 只在明确热区处理鼠标。
4. 空状态不显示默认 Fence。
5. 绘制临时框选和确认 UI。
6. 为后续 `13` 的编辑热区保留接口。

## 验收标准

- 桌面图标双击、右键正常。
- 任务栏和托盘正常。
- 普通窗口点击正常。
- Overlay 退出后窗口销毁完整。
- 无真实 Fence 时不显示默认背景。

## 后续关系

- `12` 使用 Overlay 做框选和确认。
- `10` 负责真实背景。
- `13` 使用 Overlay 做前景提示和编辑热区。
