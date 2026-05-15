# 01 Explorer 控制主导桌面分组实现

## 目标

以 **Explorer 控制主导** 的方式，实现接近市面上 `Fences` 软件的桌面分组能力：

- Windows 继续原生绘制桌面图标
- 应用负责 Fence 背景显示、图标归属关系、图标坐标控制、Fence 编辑与恢复
- 用户最终感知到的是“图标被 Fence 托举并组织起来”，而不是“桌面上又盖了一层会抢输入的窗口”

## 为什么转向 Explorer 控制主导

如果继续让一个全屏 Overlay 同时承担：

- 画矩形
- 接鼠标
- 拖拽缩放
- 控制桌面交互
- 甚至尝试插进 WorkerW 层级

就会持续遇到：

- Fence 可见性不稳定
- 桌面图标和 Fence 层级不稳定
- 托盘、任务栏、普通窗口右键被影响
- Explorer 重启后行为混乱

而市面上的 Fences 类产品，更像是：

- 继续使用 Explorer 作为桌面图标真实显示层
- 自己额外绘制 Fence 容器
- 自己维护图标与 Fence 的归属
- 自己控制桌面图标坐标和恢复

这就是本项目新的主方向。

## 总体方案

### 1. Explorer 桌面控制层

职责：

- 解析桌面窗口拓扑
- 找到 `SysListView32`
- 枚举桌面图标
- 控制桌面图标坐标
- 响应 Explorer 重启并重连

### 2. Fence 背景层

职责：

- 绘制 Fence 背景、边框、标题
- 标记当前活动 Fence
- 提供编辑态视觉提示

原则：

- 只画，不大面积接输入

### 3. 编辑热区层

职责：

- 处理小范围的移动、缩放、删除、重命名入口
- 避免全屏交互层破坏系统行为

### 4. 运行时与数据层

职责：

- 保存 Fence 集合
- 保存图标归属关系
- 保存原始坐标与当前目标坐标
- 管理恢复策略与异常退出状态

## 核心对象模型

### Fence

- `fence_id`
- `title`
- `bounds`
- `display_order`
- `is_locked`
- `style_json`

### FenceIcon

- `fence_id`
- `icon_identity`
- `original_x`
- `original_y`
- `current_x`
- `current_y`
- `slot_index`

### DesktopResolveResult

- `progman`
- `workerw`
- `defview`
- `listview`
- `explorer_pid`
- `resolve_path`

### RestoreSession

- `last_shutdown_clean`
- `last_restore_needed`
- `last_restore_mode`

## 核心闭环

### 创建闭环

1. 用户在桌面空白区发起框选
2. 应用识别命中的桌面图标
3. 创建 Fence
4. 记录图标原始坐标
5. 计算 Fence 内目标坐标
6. 批量移动图标到 Fence 区域
7. 绘制 Fence 背景
8. 保存 Fence 与 FenceIcons

### 编辑闭环

1. 用户选中当前 Fence
2. 用户移动 / 缩放 / 重命名 / 删除
3. 应用更新 Fence 几何与数据
4. 重新计算图标目标位置
5. 批量移动桌面图标
6. 持久化结果

### 恢复闭环

1. 退出时根据策略决定是否恢复原始桌面
2. 启动时重新连接 Explorer
3. 重新加载 Fence 与图标关系
4. 重新匹配当前桌面图标
5. 重新应用 Fence 布局
6. Explorer 重启后走同样的重建流程

## 关键技术判断

### 1. 不再追求“把 Overlay 硬塞到图标下层”

不要再把 `SetParent(WorkerW)` 当作主方案。

因为它可能导致：

- Fence 不可见
- 框选起手失效
- 输入边界失控

### 2. 继续使用 Explorer 绘制真实图标

这意味着：

- 保留 Windows 原生图标双击与右键基础行为
- 保持和真实桌面对象的一致性
- 但我们必须承受更高的 Explorer 共存复杂度

### 3. 输入必须尽量小范围处理

不要让全屏层承担复杂交互。

正确方向是：

- 背景层只画
- 框选只在桌面空白区起手
- 编辑只靠小范围热区

## 最终验收标准

1. 桌面图标仍由 Explorer 原生绘制。
2. 图标可以稳定显示在 Fence 区域之上。
3. Fence 不破坏桌面、任务栏、托盘、普通窗口的正常右键与点击。
4. Fence 创建、移动、缩放、删除、重命名形成闭环。
5. 多 Fence 下图标布局与归属关系可稳定持久化。
6. 退出、重启、Explorer 重启、异常退出后都可恢复。

## 结论

本项目不再把方向放在“自绘一层新的图标桌面”，而是明确转向：

`以 Explorer 原生桌面图标为真实显示层，以应用作为 Fence 容器控制器和图标布局控制器。`

这是最接近市面上 Fences 类产品体验、也最符合你当前目标的路线。
