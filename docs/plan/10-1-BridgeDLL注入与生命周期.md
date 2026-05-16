# 10-1 Bridge DLL 注入与生命周期

## 目标

建立 `ExplorerBridge.dll` 的最小可运行骨架，并验证主程序可以安全地把它加载到 Explorer 进程中。

## 执行步骤

1. 新增 DLL target：`ExplorerBridge.dll`。
2. DLL `DllMain` 只做轻量初始化，不做复杂工作。
3. DLL 启动后台工作线程或消息窗口。
4. DLL 写独立日志，标明 Explorer PID、线程 ID、加载时间。
5. 主程序新增 `ExplorerBridgeManager`。
6. 主程序定位 Explorer PID。
7. 主程序通过受控注入方式加载 DLL。
8. 主程序能检测 DLL 是否已经加载，避免重复注入。
9. 主程序退出时发送 shutdown 或清理请求。

## 验收标准

- 构建生成 exe 和 dll。
- 启动后 ExplorerBridge 日志出现。
- 重复启动不会重复注入多份不可控实例。
- 退出应用后 Bridge 不继续持有错误状态。
- Explorer 重启后主程序能识别 Bridge 已失效。

## 风险

- 注入失败不能影响主程序基础功能。
- DLL 初始化不能卡住 Explorer。
- 调试阶段必须保留清理和重启 Explorer 的应急流程。
