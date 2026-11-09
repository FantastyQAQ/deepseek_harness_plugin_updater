# DSH 插件更新工具

基于 Qt Widgets 的 Windows 桌面小工具，用于批量更新 DeepSeek Harness（DSH）已安装的插件：扫描全部 profile 的插件依赖，查询 npm 注册表最新版本并对比，经官方通道 `dsh plugin add` 串行更新，支持一键全部更新或仅更新勾选项。

## 功能特性

- 扫描 `%USERPROFILE%\.dsh\profiles\*\package.json` 的 `dependencies`，列出全部已安装插件
- 用 `npm view <pkg> dist-tags.latest` 查询注册表最新版本（60 秒看门狗超时强杀），与本地 `node_modules/<pkg>/package.json` 现版本对比
- 一键更新全部或仅更新勾选插件，经官方通道 `dsh plugin --profile <name> add <pkg>@latest` 串行执行
- Git 源 / 本地路径源不做版本检查，直接标记「无法检查」
- Fusion 风格 + 内联 QSS 浅色主题，界面布局与配色参照同作者的 Git 批量拉取工具

## 使用方法

1. 启动后自动扫描全部 DSH profile，列出已安装插件及版本状态
2. 点击「刷新」重新扫描；勾选需要更新的插件
3. 点击「一键更新全部」或「更新选中」，全程可通过「停止」中止

## 构建环境

- Qt 5.12.8 + MinGW 7.3 32 位（`D:\sw\Qt5.12.8\5.12.8\mingw73_32`），C++17，qmake 构建
- 生成 Makefile（**必须在纯 cmd 环境**，Git Bash / MSYS2 会让 qmake 生成跑不通的 Makefile）：
  ```cmd
  set PATH=D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin;D:\SW\Qt5.12.8\Tools\mingw730_32\bin;C:\Windows\system32;C:\Windows
  D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin\qmake.exe dsh_plugin_updater.pro -spec win32-g++
  ```
- 编译（Release，产物在 `release\`）：`mingw32-make release`

---

# DSH Plugin Updater (English)

A Qt Widgets desktop tool for Windows that batch-updates plugins installed in DeepSeek Harness (DSH): it scans all profiles' plugin dependencies, queries the npm registry for the latest versions, compares them with the installed ones, and updates via the official `dsh plugin add` channel serially — either all at once or only the checked ones.

## Features

- Scans `dependencies` of `%USERPROFILE%\.dsh\profiles\*\package.json` and lists all installed plugins
- Queries the registry with `npm view <pkg> dist-tags.latest` (60-second watchdog kill on timeout) and compares against the local `node_modules/<pkg>/package.json` version
- Updates all or only checked plugins, serially via the official channel `dsh plugin --profile <name> add <pkg>@latest`
- Git-sourced / local-path plugins skip version checks and are marked "not checkable"
- Fusion style + inline QSS light theme; layout and colors follow the author's Git Batch Pull Tool

## Usage

1. All DSH profiles are scanned automatically on startup, listing installed plugins and their version status
2. Click "Refresh" to rescan; check the plugins you want to update
3. Click "Update All" or "Update Selected"; press "Stop" to abort anytime

## Build

- Qt 5.12.8 + MinGW 7.3 32-bit, C++17, qmake
- Generate Makefile (**in a plain cmd shell** — Git Bash / MSYS2 makes qmake produce broken Makefiles):
  ```cmd
  set PATH=D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin;D:\SW\Qt5.12.8\Tools\mingw730_32\bin;C:\Windows\system32;C:\Windows
  D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin\qmake.exe dsh_plugin_updater.pro -spec win32-g++
  ```
- Build (Release, output in `release\`): `mingw32-make release`

---

## License / 许可证

本项目以 MIT 许可证发布（以下为英文原文，具有法律效力；中文仅供参考）。
This project is released under the MIT License (the English text below is the authoritative version; the Chinese note is for reference only — roughly: anyone may use, copy, modify and distribute this software, provided the copyright notice is preserved, and the authors accept no liability).

```text
MIT License

Copyright (c) 2026 欧力 / lch (fantasylch@foxmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

# 更新记录 / Change Log

完整修改记录见 [docs/README.zh_CN.md](docs/README.zh_CN.md)（程序内「更新说明」窗口同源显示）。
Full change log: [docs/README.zh_CN.md](docs/README.zh_CN.md) (same source as the in-app "Update Notes" window).
