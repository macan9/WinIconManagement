# 12-0 框选创建单个 Fence

## 目标

在 `10` 的真实背景路径和 `11` 的 Overlay 前景边界稳定后，完成第一个用户可见业务闭环：

`框选图标 -> 创建 Fence -> 移动图标 -> 保存关系 -> Bridge 绘制背景 -> Overlay 显示前景提示。`

## 为什么放在 10 和 11 之后

如果先做创建 Fence，再补真实背景，就会让验收标准混乱：

- 用户看到的 Fence 背景可能仍是 Overlay 假背景。
- 图标上下层级问题会反复干扰创建闭环。
- 背景不可见时难以判断创建是否真正成功。

因此，框选创建必须放在 Explorer Bridge 背景路径和 Overlay 前景边界之后。

## 执行流程

1. 鼠标在桌面空白区域起手。
2. Overlay 绘制临时框选矩形。
3. 松开鼠标后识别命中的桌面图标。
4. 根据框选区域生成 Fence bounds。
5. 计算图标在 Fence 内的目标布局。
6. 移动图标到目标坐标。
7. 保存 Fence 记录。
8. 保存 FenceIcon 关系、原始坐标和当前坐标。
9. 刷新运行时状态。
10. 通过 IPC 通知 Bridge 更新背景。
11. 通知 Overlay 更新前景提示。

## 不做

- 不实现多 Fence。
- 不实现 Fence 编辑提交。
- 不实现 Bridge 注入本身。
- 不通过 BackgroundWindow 或 Overlay 假装最终背景。

## 验收标准

- 可以创建一个 Fence。
- 命中的图标进入 Fence 区域。
- 未命中的图标不受影响。
- 数据库中保存 Fence 和 FenceIcon。
- Bridge 收到 Fence bounds 并绘制背景。
- Overlay 只显示前景提示。
- 重启后能加载该 Fence 数据。
