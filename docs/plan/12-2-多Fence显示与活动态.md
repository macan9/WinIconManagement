# 10-2 多 Fence 显示与活动态

## 目标

让背景层能同时绘制多个 Fence，并清晰表达显示顺序、活动态和锁定态。

## 开发内容

- `OverlayWindow` 支持绘制 Fence 集合
- 为每个 Fence 传入：
  - bounds
  - title
  - active state
  - locked state
  - display order
- 定义多 Fence 显示顺序
- 定义活动 Fence 高亮样式
- 定义锁定 Fence 与可编辑 Fence 的视觉差异
- 优化多 Fence 重绘，避免每次全量无意义刷新

## 验收标准

- 多个 Fence 可以同时显示
- 当前活动 Fence 有明确视觉状态
- 显示顺序稳定
- 添加、删除、切换活动 Fence 后背景层能正确刷新

## 风险点

- Fence 重叠时显示顺序和命中顺序需要一致
- 背景层不应因为多 Fence 绘制而开始拦截桌面输入

