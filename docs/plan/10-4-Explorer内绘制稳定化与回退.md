# 10-4 Explorer 内绘制稳定化与回退

## 目标

当 `LVM_SETBKIMAGEW` 不足以实现目标时，验证更重的 Explorer 内绘制路径。

## 候选路径

### SysListView32 subclass

思路：

- Bridge 在 Explorer 内 subclass 桌面 ListView。
- 在合适的 paint 阶段绘制 Fence 背景。
- 保持 Explorer 原生图标绘制不变。

风险：

- 绘制时序复杂。
- Explorer 主题、刷新、DPI、图标文字背景可能受影响。
- 必须保证卸载时还原 WndProc。

### SHELLDLL_DefView subclass

思路：

- 在 DefView 层处理背景绘制或重绘触发。
- 避免直接干扰 ListView 图标项绘制。

风险：

- 可能仍被 ListView 覆盖。
- 不同 Windows 版本行为可能不同。

## 执行步骤

1. 先做只记录消息的 subclass，不绘制。
2. 找到安全绘制时机。
3. 绘制单 Fence 最小背景。
4. 验证图标绘制、选中、拖拽、右键。
5. 验证 Explorer 重启、DPI、多显示器。
6. 实现 clear/unsubclass。

## 验收标准

- 背景在图标下方。
- 不破坏 Explorer 原生交互。
- 卸载后 Explorer 状态恢复。
- 崩溃风险可控，有明确回退开关。

## 回退策略

如果进程内绘制风险不可控：

- 保留 Overlay 前景提示。
- 背景绘制降级为可选实验功能。
- 产品主线继续保证图标分组、布局和恢复能力。
