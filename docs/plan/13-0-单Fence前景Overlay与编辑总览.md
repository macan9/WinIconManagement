# 13-0 单 Fence 前景 Overlay 与编辑总览

## 目标

在 `10` 的真实背景绘制路径、`11` 的 Overlay 边界和 `12` 的框选创建闭环稳定后，收口单 Fence 的前景提示和编辑入口。

Overlay 只承担：

- 边框
- 标题
- 活动态
- 删除按钮
- 缩放手柄
- 框选确认 UI

Overlay 不再承担真实背景填充。

## 执行顺序

1. 清理 Overlay 中仍像背景层的绘制职责。
2. 对齐 Bridge 背景层与 Overlay 前景层的几何数据。
3. 收口活动 Fence 的标题、边框和操作按钮。
4. 验证 Overlay 默认 click-through。
5. 进入 `13-1` 做编辑热区和预览态。

## 验收标准

- 背景由 `10` 的 Explorer Bridge 路径负责。
- Overlay 只画前景提示。
- Overlay 不遮挡任务栏和普通窗口。
- 桌面图标点击、拖动、右键仍交给 Explorer。

## 下一步

- [13-1-单Fence编辑热区与预览态.md](./13-1-单Fence编辑热区与预览态.md)
