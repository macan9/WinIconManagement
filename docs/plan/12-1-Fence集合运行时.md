# 10-1 Fence 集合运行时

## 目标

把单 Fence 状态正式升级为 Fence 集合运行时，为后续多 Fence 显示、编辑、加载和恢复提供统一内存模型。

## 开发内容

- 将单个 `activeFence` / `currentFence` 状态改为 Fence 集合
- 为每个 Fence 维护稳定 `fence_id`
- 区分运行时 Fence 类型：
  - 正式 Fence
  - 临时框选 Fence
  - 待确认 Fence
  - 当前活动 Fence
- 建立 Fence 查询接口：
  - 按 id 查询
  - 按点命中查询
  - 查询当前活动 Fence
- 建立 Fence 集合变更入口：
  - 新增
  - 更新
  - 删除
  - 设置活动项

## 验收标准

- 运行时可以同时持有多个 Fence
- 当前活动 Fence 概念稳定
- 旧的单 Fence 操作能通过集合接口完成
- 不再依赖“默认第一条 Fence”作为隐式真源

## 风险点

- 单 Fence 时代遗留状态容易和新集合状态并存，形成双真源
- 活动 Fence 切换如果不清晰，后续编辑态会混乱

