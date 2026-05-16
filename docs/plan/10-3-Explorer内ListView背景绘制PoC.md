# 10-3 Explorer 内 ListView 背景绘制 PoC

## 目标

在 Explorer 进程内验证第一条真实背景绘制路径：

`Bridge 生成背景 bitmap，并在 Explorer 进程内应用到桌面 SysListView32。`

## 优先路径

先尝试 `LVM_SETBKIMAGEW`。

之前跨进程调用不可靠，核心问题包括：

- HBITMAP 生命周期不在 Explorer 内。
- ListView 消息跨进程传参不可靠。
- 资源释放边界不清楚。

Bridge 在 Explorer 内后，这些问题可以重新验证。

## 执行步骤

1. Bridge 定位 `SHELLDLL_DefView / SysListView32`。
2. 根据虚拟桌面尺寸创建 DIBSection。
3. 把 Fence 背景绘制到 bitmap。
4. 调用 `LVM_SETBKIMAGEW`。
5. 触发 ListView 重绘。
6. 主程序退出或 `CLEAR` 时清理背景。

## 验收标准

- Fence 背景肉眼显示在图标下方。
- 图标文字、选中、拖动、右键仍由 Explorer 原生处理。
- 任务栏不被遮挡。
- 刷新桌面后背景仍可恢复。
- 清理后桌面恢复原样。

## 如果失败

如果 `LVM_SETBKIMAGEW` 在 Explorer 内仍无法满足目标，进入 `10-4` 的 subclass 绘制路径。
