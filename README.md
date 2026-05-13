# WinIconManagement

轻量级 Windows 桌面图标管理工具。

项目采用 **C++20 + Win32 原生实现**，目标是在尽量低的 CPU 和内存占用下，管理、分类和美化 Windows 桌面图标。

## 技术路线

- C++20
- Win32 Desktop App
- MSVC + Windows SDK
- CMake + Ninja
- `Shell_NotifyIcon` 系统托盘
- Explorer 原生桌面图标方案

## 当前进度

- 已完成原生 Win32 项目骨架
- 已完成单实例保护
- 已完成基础日志
- 已完成托盘图标和右键菜单
- 已完成 VS Code CMake / Debug 配置

## 构建

先加载 MSVC 环境：

```powershell
cmd /s /c """D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && powershell"
```

首次构建：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

运行：

```powershell
.\build\WinIconManagement.exe
```

## VS Code 调试

打开项目：

```powershell
code .
```

常用操作：

- `CMake: Configure`
- `CMake: Build`
- `F5` 调试运行

## 文档

- [核心实现](docs/核心实现.md)
- [实现架构](docs/实现架构.md)
- [开发环境](docs/开发环境.md)
- [运行与构建](docs/运行构建.md)
- [日志查看](docs/日志查看.md)
- [调试流程](docs/调试流程.md)
- [开发计划](docs/plan)

## Git 提醒

查看状态：

```powershell
git status --short --branch
```

提交：

```powershell
git add .
git commit -m "提交说明"
git push
```

提交前建议先构建：

```powershell
cmake --build build
```
