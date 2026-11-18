# 在线更新方案（Online Update）

> 状态：定稿（路线 A），待实施
> 适用工程：dsh_plugin_updater（Qt 5.12.8 + MinGW 7.3 32 位 + C++17）
> 目标版本：SV01.010 起

## 〇、已确认决策

| 事项 | 结论 |
|---|---|
| 更新顺序 | 先在更新窗口内下载（显示进度条），校验通过后再关闭旧程序并替换启动 |
| 服务器 | 中国大陆阿里云 ECS（Linux），采用路线 A：非标准端口 + HTTP，不备案，见第五节 |
| 不可写目录 | 接受兜底：另存到桌面，由用户手动替换 |
| 启动检查 | 不自动检查，仅由用户点击「在线更新」按钮触发 |
| 代码签名 | 本期不做 Authenticode 签名，见第九节 |
| 编译失败 | 不纳入本方案的分发风险范围，静态编译失败不作为退路 |
| 分发形态 | 单 exe，Qt 静态链接，不安装，直接运行 |

## 一、目标与范围

**要做的**

- 主窗口工具栏「关于」按钮右侧新增「在线更新」按钮。
- 点击后弹出「在线更新」窗口，检查服务器上是否有新版本。
- 有新版本时显示版本号与更新说明，用户点击「立即更新」后开始下载，窗口内显示进度条。
- 下载并校验通过后，关闭旧程序，替换 exe，自动启动新版本。
- 程序放在不可写目录时，提供「另存到桌面、手动替换」的兜底。

**本期不做**

- 启动时自动检查更新。
- Authenticode 代码签名。
- 增量更新、多渠道、版本回退。
- DSH 插件本身的更新（那是 `PluginManager` 已有的功能，与本方案无关）。

## 二、更新顺序说明

需求的字面顺序是「确认 → 关闭旧程序 → 下载」。本方案采用的顺序是：

**确认 → 在更新窗口内下载（进度条）→ 校验通过 → 提示「即将关闭并重启」→ 关闭旧程序 → 替换 → 启动新版**

理由：

- 下载失败（断网、服务器异常）时旧程序仍可用，不会出现程序退出后无法恢复的情况。
- 进度窗口始终在同一个进程内，无需额外的更新助手进程。
- 从用户视角看，仍然是「确认后下载，下载完成后关闭旧程序并启动新版」。

## 三、关键决策汇总

| 项 | 选择 | 理由 |
|---|---|---|
| 分发形态 | 单 exe，Qt 静态链接 | 用户要求，直接运行 |
| 传输协议 | HTTP，使用非标准端口（路线 A） | 阿里云大陆机未备案时 80/443 受限，见第五节 |
| 完整性保证 | 清单 RSA-2048 签名 + exe 的 SHA256 + 大小校验 | HTTP 明文无法保证内容可信，签名是核心 |
| 验签实现 | Windows 自带 BCrypt API | 不引入 OpenSSL，静态编译更简单 |
| 自替换方式 | 重命名旧 exe，再把新 exe 改为原名 | 无需 bat、无需 cmd，逻辑内聚，可回滚 |
| 服务器 | 阿里云 ECS + Nginx 静态文件托管 | 不需要后端程序，维护成本最低 |
| 不可写目录 | 另存到桌面，用户手动替换 | 已确认的兜底方案 |
| 代码签名 | 不做 | 已确认，见第九节 |
| 触发方式 | 仅手动点击「在线更新」 | 已确认，不做启动自动检查 |

## 四、总体架构

**客户端**（`dsh_plugin_updater.exe`）

- 主窗口工具栏：`[关于] [在线更新] [更新说明] [退出]`
- `OnlineUpdateDialog`：窗口与状态机，串起以下模块
  - `UpdateManifest`：解析 `latest.json`，解析版本号并比较
  - `UpdateVerifier`：RSA 验签（BCrypt）、文件 SHA256
  - `UpdateDownloader`：下载 exe，发出进度信号，支持取消与停滞超时
  - `UpdateApplier`：目录可写性检查、重命名替换、回滚、启动新版、清理残留

**服务器**（阿里云 ECS，Linux）

- Nginx 监听一个非标准端口（示例 8080，阶段 0 确认可达后定稿），托管 `/srv/updates/dsh_plugin_updater/`
- 目录中包含 `latest.json`、`latest.json.sig` 和 `releases/<版本>/dsh_plugin_updater.exe`

**数据流**

```
客户端 ──GET latest.json / latest.json.sig──► 阿里云 ECS（Nginx，非标准端口）
客户端 ◄──────────── 清单 + 签名 ─────────────┘
客户端：验签 → 比较版本 → 用户确认
客户端 ──GET releases/<版本>/dsh_plugin_updater.exe──► 阿里云 ECS（带进度）
客户端：大小与 SHA256 校验 → 窗口提示 → 关闭旧程序 → 替换 → 启动新版
```

## 五、服务器端部署（阿里云 ECS，路线 A）

### 5.1 端口与备案说明

阿里云中国大陆服务器对通过 **80 / 443 端口**对外提供的网站有备案检查，未备案的服务器这两个端口会被拦截。

路线 A 的做法是：**不使用 80/443，改用非标准端口提供 HTTP 服务，不做 ICP 备案。**

实施要求：

- 端口选择：示例使用 `8080`。最终端口由阶段 0 实测确定，应尽量选择非常用端口（如 `8080` 不通可换其它高位端口）。
- **阶段 0 必须实测**：用手机 4G/5G 网络（与服务器所在网络不同）访问 `http://<公网IP>:<端口>/dsh_plugin_updater/latest.json`，确认外网可达。
- 实测不通时，路线 A 不成立，需要回到方案决策（转路线 B：域名 + 备案）。

### 5.2 云控制台与系统准备

- **ECS 安全组**：放行所选端口（示例 TCP 8080）。
- **系统防火墙**（firewalld / ufw）：同步放行同一端口，两层都要开。
- **SSH**：仅用于上传文件，建议密钥登录，禁用密码登录。

### 5.3 安装 Nginx

Alibaba Cloud Linux / CentOS / Rocky：

```bash
sudo dnf install -y nginx
```

Ubuntu / Debian：

```bash
sudo apt update && sudo apt install -y nginx
```

### 5.4 目录结构

```
/srv/updates/dsh_plugin_updater/
├── latest.json                        # 清单，每次检查都读取最新内容
├── latest.json.sig                    # 清单签名
└── releases/
    ├── SV01.009/dsh_plugin_updater.exe
    └── SV01.010/dsh_plugin_updater.exe   # 发布后不再修改
```

```bash
sudo mkdir -p /srv/updates/dsh_plugin_updater/releases
sudo chown -R $USER /srv/updates
sudo chmod -R a+rX /srv/updates
```

上传用户可写，Nginx 进程可读。

### 5.5 Nginx 配置

新建 `/etc/nginx/conf.d/dsh_updates.conf`（示例端口 8080，以实测确定的端口为准）：

```nginx
server {
    listen 8080 default_server;
    server_name _;

    root /srv/updates;
    autoindex off;

    # 清单必须每次都取最新，禁止缓存
    location ~ ^/dsh_plugin_updater/latest\.json(\.sig)?$ {
        add_header Cache-Control "no-cache";
    }

    # 版本目录一经发布不再修改，可以长期缓存
    location /dsh_plugin_updater/releases/ {
        add_header Cache-Control "public, max-age=31536000";
    }

    # 其它路径一律 404
    location / {
        return 404;
    }
}
```

生效：

```bash
sudo nginx -t && sudo systemctl reload nginx
```

Nginx 对静态文件会返回 `Content-Length`，客户端据此计算下载进度百分比。

### 5.6 清单 latest.json

```json
{
  "schema": 1,
  "app": "dsh_plugin_updater",
  "version": "SV01.010",
  "size": 15728640,
  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "url": "releases/SV01.010/dsh_plugin_updater.exe",
  "publishedAt": "2026-11-20T10:00:00+08:00",
  "notes": "- 新增在线更新功能\n- 修复若干界面问题"
}
```

| 字段 | 说明 |
|---|---|
| `schema` | 清单格式版本，当前为 1，客户端遇到不认识的版本直接拒绝 |
| `app` | 应用标识，必须等于 `dsh_plugin_updater`，防止拿错清单 |
| `version` | 显示版本号，客户端取末尾数字比较（`SV01.010` → `10`） |
| `size` | exe 字节数，用于进度总量与完整性检查 |
| `sha256` | exe 的 SHA256，小写十六进制 |
| `url` | 相对路径，相对于客户端内置的 `UPDATE_BASE_URL` 解析 |
| `publishedAt` | 发布时间，仅用于展示 |
| `notes` | 更新说明，`\n` 换行，弹窗中以纯文本显示 |

清单使用相对路径，便于将来更换地址时不必重写已发布的清单。

### 5.7 验证

```bash
curl -I http://<公网IP>:8080/dsh_plugin_updater/latest.json
curl -s  http://<公网IP>:8080/dsh_plugin_updater/latest.json
```

返回 200 且内容正确即通过。Windows 下可用 PowerShell 的 `Invoke-WebRequest` 代替。**外网验证请使用手机网络，不要只在服务器所在网络内测试。**

### 5.8 端口与地址的长期约定

- 已分发的客户端内置的是 `IP + 端口`，**端口一经确定不要更改**，否则老版本无法获取更新。
- 公网 IP 如果变化（如 ECS 重新分配），同样会导致老版本失效。建议为 ECS 绑定弹性公网 IP（EIP），保持地址稳定。
- 将来若要切换到域名（需要备案），需要重新评估，届时另行出方案。

## 六、客户端设计

### 6.1 配置与常量

新增 `user/inc/updateconfig.h`，集中放置可调参数：

```cpp
// 更新源基址：IP 与端口在阶段 0 确认后定稿，之后不要更改
#define UPDATE_BASE_URL          "http://<公网IP>:8080/dsh_plugin_updater/"
// 检查清单的超时（毫秒）
#define UPDATE_CHECK_TIMEOUT_MS  10000
// 下载过程中连续无数据的超时（毫秒），Qt 5.12 没有 setTransferTimeout，需自行实现
#define UPDATE_STALL_TIMEOUT_MS  30000
```

RSA 公钥（模数的十六进制与指数）放在 `user/src/updateverifier.cpp` 的静态数组中，不放入头文件的宏里。

版本号不新增宏，直接解析现有的 `APP_VERSION`（`SV01.009`）末尾数字。这样仍然只有一个版本来源，符合 AGENTS.md 中「三个宏同步」的规则。

### 6.2 界面

**主工具栏**：在「关于」之后插入「在线更新」按钮，颜色使用橙色系，与现有按钮风格一致（参照 `mainwindow.cpp` 中 `m_aboutBtn` 的写法）：

```cpp
m_onlineUpdateBtn->setStyleSheet(QStringLiteral(
    "QPushButton { background: #fff4e0; border-color: #f5d9a8; color: #b36b00; }"
    "QPushButton:hover { background: #ffe9c2; border-color: #e8b766; }"));
```

**在线更新窗口**（`OnlineUpdateDialog`，模态，宽约 480 px）：

```
┌ 在线更新 ─────────────────────────────────┐
│ 当前版本：SV01.009                         │
│ 最新版本：SV01.010    发布于 2026-11-20    │
│ 更新说明：                                 │
│ ┌────────────────────────────────────────┐ │
│ │ - 新增在线更新功能                      │ │
│ │ - 修复若干界面问题                      │ │
│ └────────────────────────────────────────┘ │
│ [██████████░░░░░░░░░░]  52%               │
│ 已下载 8.1 MB / 15.0 MB   3.2 MB/s         │
│ 状态：正在下载…                            │
│                 [检查更新] [立即更新] [取消] │
└───────────────────────────────────────────┘
```

按钮行为：

- 「检查更新」：开始检查，检查中禁用。
- 「立即更新」：仅在发现新版本时可用，点击后开始下载。
- 「取消」：下载中变为「取消下载」，取消后删除未完成的临时文件；其它时候为「关闭」。
- 下载、校验或替换进行中，窗口不允许通过关闭按钮退出（`closeEvent` 中拦截）。

### 6.3 状态机

| 状态 | 进入条件 | 界面表现 |
|---|---|---|
| `Idle` | 打开窗口 | 显示当前版本，按钮「检查更新」可用 |
| `Checking` | 点击检查更新 | 进度条忙碌态，状态「正在检查…」 |
| `UpToDate` | 远端版本 ≤ 本地版本 | 状态「已是最新版本」 |
| `Available` | 远端版本 > 本地版本，且清单验签通过 | 显示新版本与更新说明，「立即更新」可用 |
| `Downloading` | 点击立即更新 | 进度条 0–100，显示已下载量与速度 |
| `Verifying` | 下载完成 | 进度条忙碌态，状态「正在校验…」 |
| `Applying` | 校验通过且目录可写 | 状态「即将关闭并重启」，随后退出 |
| `ManualInstall` | 校验通过但目录不可写 | 进入兜底流程，见 6.8 节 |
| `Error` | 任何步骤失败 | 红色错误信息，按钮恢复为「检查更新」 |

### 6.4 文件清单

| 文件 | 职责 |
|---|---|
| `user/inc/updateconfig.h` | 更新源地址与超时参数（单行注释） |
| `user/inc/updatemanifest.h` / `user/src/updatemanifest.cpp` | 解析 `latest.json`，解析版本号并比较 |
| `user/inc/updateverifier.h` / `user/src/updateverifier.cpp` | RSA 验签（BCrypt）、文件 SHA256 计算 |
| `user/inc/updatedownloader.h` / `user/src/updatedownloader.cpp` | 基于 QNetworkAccessManager 下载，发出进度，支持取消与停滞超时 |
| `user/inc/updateapplier.h` / `user/src/updateapplier.cpp` | 目录可写性检查、自替换、失败回滚、启动新版、清理 `.old` |
| `user/inc/onlineupdatedialog.h` / `user/src/onlineupdatedialog.cpp` | 窗口与状态机，串起以上模块 |
| `user/src/mainwindow.cpp` | 新增按钮与 `on_online_update()` 槽 |
| `user/src/main.cpp` | 启动时调用 `UpdateApplier::cleanupLeftovers()` |
| `dsh_plugin_updater.pro` | 增加 `QT += network`、`LIBS += -lbcrypt`、`QTPLUGIN` 等 |

源文件函数注释使用 Doxygen 风格，头文件只写单行说明，符合 AGENTS.md 约定。

### 6.5 检查流程

1. 请求 `UPDATE_BASE_URL + "latest.json"`，超时 `UPDATE_CHECK_TIMEOUT_MS`。缓存策略设为 `AlwaysNetwork`，避免读到旧缓存。
2. 请求 `latest.json.sig`。
3. 用内置公钥验签 `latest.json` 的**原始字节**。失败则直接报错「更新源校验失败」，不解析内容。
4. 解析 JSON，检查 `schema == 1` 且 `app == "dsh_plugin_updater"`。
5. 比较版本：远端末尾数字 > 本地末尾数字才提示更新。远端更低或相同则不做任何事，**不做降级**。
6. 用 `QUrl(UPDATE_BASE_URL).resolved(QUrl(manifest.url))` 得到 exe 的完整地址。

### 6.6 下载与校验

- **临时文件**：下载到 `<exe所在目录>\<exe文件名>.new`，与 exe 同目录，保证后续重命名在同一卷上完成。
- **可写性预检**：开始下载前，先尝试在 exe 所在目录创建 `.new` 文件。失败则进入 6.8 节的兜底流程，并且不浪费流量去下载。
- **进度**：连接 `QNetworkReply::downloadProgress(received, total)`。若 `total == -1`，进度条切换为忙碌态。
- **速度**：每 500 ms 采样一次，用字节差除以时间差计算。
- **停滞超时**：Qt 5.12 没有 `setTransferTimeout`（5.15 才有），需要用单次 `QTimer`，每收到数据重置，超过 `UPDATE_STALL_TIMEOUT_MS` 则 `reply->abort()`。
- **重定向**：`setAttribute(QNetworkRequest::FollowRedirectsAttribute, true)`。
- **取消**：`reply->abort()` 后删除 `.new`。
- **并发**：同一时间只允许一个下载任务。
- **完成后校验**：
  - 实际字节数必须等于 `manifest.size`；
  - SHA256 必须等于 `manifest.sha256`（用 `QCryptographicHash` 分块计算，避免整个文件读入内存）。
  - 任一不符则删除 `.new`，报错。

### 6.7 替换与重启（重命名方案）

Windows 不允许覆盖或删除正在运行的 exe，但**允许重命名**。利用这一点，无需 bat 脚本。

设 exe 路径为 `E:\tools\dsh_plugin_updater.exe`（通过 `QCoreApplication::applicationFilePath()` 获取，不写死文件名）：

1. 下载与校验通过，`.new` 文件已位于同目录。
2. 检查 `PluginManager` 的 `m_activeProc` 与 `m_queryProc` 是否在运行。若在运行，提示「请等待插件更新完成后再更新程序」，停止替换。
3. 将 `dsh_plugin_updater.exe` 重命名为 `dsh_plugin_updater.exe.old`（运行中允许）。
4. 将 `dsh_plugin_updater.exe.new` 重命名为 `dsh_plugin_updater.exe`。
5. 若第 3 步或第 4 步失败：把 `.old` 改回原名，报错，旧版继续可用。
   - 重命名可能因杀毒软件短暂占用而失败，需要重试 5 次，每次间隔 500 ms。
6. `QProcess::startDetached(exePath, {}, exeDir)` 启动新版。
7. `qApp->quit()` 退出旧进程。
8. **新版启动时清理**（`main.cpp` 中调用 `cleanupLeftovers()`）：
   - 若存在 `.old`，尝试删除。此时旧进程已经退出，通常可以删除。删除失败则忽略，下次启动再试。
   - 若存在 `.new`，直接删除（说明上次下载未完成）。

**未采用的方案**：「先关闭旧程序再下载」需要增加更新助手进程，与第二节确定的顺序不符，本期不实现。

### 6.8 不可写目录的兜底（已确认）

程序可能放在 `C:\Program Files`、只读介质等不可写目录。此时采用「另存到桌面、手动替换」：

1. 预检发现 exe 所在目录不可写。
2. 在更新窗口中提示：「当前程序所在目录不可写（可能是 Program Files 或只读介质）。新版本已下载到桌面，请关闭本程序后，将桌面上的新版本覆盖原程序。」
3. 新版文件保存为 `<桌面>\dsh_plugin_updater_SV01.010.exe`，桌面路径用 `QStandardPaths::DesktopLocation` 获取。
4. 旧程序**不关闭**，继续可用。用户手动替换后再启动新版。
5. 另存文件同样经过大小与 SHA256 校验后才提示成功。

### 6.9 异常处理一览

| 场景 | 处理 |
|---|---|
| 无网络 / 服务器不可达 | 错误提示，允许重试 |
| 清单签名无效 | 拒绝，提示「更新源校验失败」 |
| 清单 `app` / `schema` 不匹配 | 拒绝，提示「更新清单格式不符」 |
| 远端版本 ≤ 本地版本 | 显示「已是最新版本」 |
| 下载中断或停滞超时 | 报错，删除 `.new` |
| 用户取消下载 | 删除 `.new`，回到 Idle |
| 实际大小或 SHA256 不符 | 删除 `.new`，报错 |
| 目录不可写 | 进入 6.8 兜底流程 |
| 插件更新任务正在运行 | 阻止替换，提示等待 |
| 重命名被杀毒占用 | 重试 5 次，仍失败则回滚 |
| 第二次重命名失败 | 把 `.old` 改回原名，旧版继续可用 |
| 新版启动失败 | 旧版已退出，`.old` 仍在，需用户手动改名恢复（需在提示中说明） |

## 七、Qt 5.12.8 静态编译（单 exe 的前提）

### 7.1 为什么必须做

动态链接的 exe 依赖 `Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll`、`Qt5Network.dll`、`platforms\qwindows.dll`，以及 MinGW 运行库（`libgcc_s_dw2-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`）。静态链接后这些全部打进 exe。

### 7.2 只需编译 qtbase

本工程只用到 core、gui、widgets、network，这些全部属于 **qtbase** 模块。因此只需编译 qtbase 源码，不必编译整个 Qt。

### 7.3 编译步骤

1. 从 `download.qt.io` 的 Qt 5.12.8 submodules 目录下载 `qtbase-everywhere-src-5.12.8` 压缩包。
2. 解压到路径尽量短且不含空格的目录，例如 `D:\sw\qt-src\qtbase-5.12.8`。
3. 确认 Perl 可用（Qt 构建脚本需要）。
4. 在**纯 cmd 环境**下操作（不要在 Git Bash / MSYS2 中执行，原因见 AGENTS.md 中关于 `sh.exe` 的说明），PATH 中只放入 32 位工具链：

   ```cmd
   set PATH=D:\SW\Qt5.12.8\Tools\mingw730_32\bin;C:\Windows\system32;C:\Windows
   ```

5. 配置与编译（示意，以实际环境为准）：

   ```cmd
   configure.bat -static -release -opensource -confirm-license ^
       -nomake tests -nomake examples -no-openssl ^
       -prefix D:\sw\Qt5.12.8-static
   mingw32-make.exe -j4
   mingw32-make.exe install
   ```

   首次编译耗时较长，视 CPU 核心数通常需要 1–3 小时。

6. 用新 Qt 生成工程：

   ```cmd
   D:\sw\Qt5.12.8-static\bin\qmake.exe dsh_plugin_updater.pro -spec win32-g++
   ```

   注意保持 `release/` 目录约定不变。

**关于 `-no-openssl`**：当前方案使用 HTTP 与 BCrypt 验签，不需要 OpenSSL。路线 A 下不涉及 HTTPS，此配置长期有效。

### 7.4 工程文件改动

`dsh_plugin_updater.pro` 增加：

```
QT      += core gui widgets network
QTPLUGIN += qwindows          # 静态版必须显式链接平台插件
LIBS    += -lbcrypt           # Windows CNG，用于 RSA 验签
```

如果运行时使用了 `.ico` 格式的 `QIcon`，还需要 `QTPLUGIN += qico`。

若链接时报错找不到 `qwindows` 平台插件，在 `main.cpp` 中手动加入：

```cpp
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
```

### 7.5 验证

- 将 exe 复制到一个只有它自己的空目录，在没有安装 Qt 的 Windows 机器上双击运行，界面与动态版一致即通过。

### 7.6 风险

- 静态版 exe 体积会增大，预计在十几 MB 量级。
- 编译耗时长，依赖 Perl；工具链版本必须与 `mingw730_32` 完全一致，否则 ABI 不兼容。
- 静态链接的大体积 exe 被杀毒软件误报的概率略高，见第九节。

## 八、清单签名（本方案必做）

本期不做 Authenticode 代码签名，但**清单签名必须做**，它是更新通道安全的核心。

### 8.1 生成密钥对（只做一次）

在本地 Windows 上执行，可使用 Git 自带的 `openssl.exe`（这里只用于生成密钥，不涉及 qmake）：

```bash
openssl genrsa -out update_sign_private.pem 2048
openssl rsa -in update_sign_private.pem -pubout -out update_sign_public.pem
openssl rsa -pubin -in update_sign_public.pem -noout -modulus
```

最后一条输出 `Modulus=<十六进制>`，把它填入 `user/src/updateverifier.cpp` 的公钥数组。指数固定为 65537（`0x010001`）。

**私钥保管**：

- 私钥 `update_sign_private.pem` 只保存在本机，不放入工程目录，不上传服务器。
- 备份到离线介质（U 盘、加密压缩包）。
- 私钥泄露后，攻击者可以推送恶意更新。需要更换密钥对，并发布一个新公钥的版本，老版本无法自动升级，需要手动安装。

### 8.2 签名清单

```bash
openssl dgst -sha256 -sign update_sign_private.pem -out latest.json.sig latest.json
```

签名的是 `latest.json` 的原始字节，因此生成清单后不要再手动修改内容。

本地自检：

```bash
openssl dgst -sha256 -verify update_sign_public.pem -signature latest.json.sig latest.json
```

输出 `Verified OK` 即为通过。

### 8.3 客户端验签实现要点

使用 Windows CNG（BCrypt）API：

1. `BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0)`
2. 按 `BCRYPT_RSAPUBLIC_BLOB` 格式组装公钥：先写 `BCRYPT_RSAKEY_BLOB` 头（`Magic = BCRYPT_RSAPUBLIC_MAGIC`，`cbPublicExp = 3`，`cbModulus = 256`），再依次写入指数与模数。
3. `BCryptImportKeyPair(hAlg, NULL, BCRYPT_RSAPUBLIC_BLOB, &hKey, blob, len, 0)`
4. 对清单原始字节计算 SHA256（`QCryptographicHash`）。
5. `BCryptVerifySignature(hKey, &padInfo, hash, 32, sig, sigLen, BCRYPT_PAD_PKCS1)`，其中 `padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM`，类型为 `BCRYPT_PKCS1_PADDING_INFO`。
6. 返回 `STATUS_SUCCESS` 为通过，释放句柄。

签名算法与 `openssl dgst -sha256 -sign` 的默认输出（PKCS#1 v1.5 + SHA-256）完全对应。

## 九、代码签名：本期不做

### 9.1 不做的原因

- 已确认本期不做代码签名。
- 证书需要身份审核与年费，且 2023 年后私钥必须放在 USB Key 或云端，自动化发布并不顺手。
- 清单签名与 SHA256 校验已经覆盖了「更新内容是否来自我们」这一安全目标。

### 9.2 不签名的影响与应对

- **首次安装**（用户从浏览器下载 exe）：SmartScreen 会提示「Windows 已保护你的电脑」，用户需要点击「更多信息」→「仍要运行」。需要在使用说明中写清楚。
- **在线更新**：由程序自己写入的文件通常不会带「来自网络」标记（Mark of the Web），Windows 一般不会对它再次弹出 SmartScreen。**这一点需要在阶段 5 实测确认。**
- **杀毒误报**：自替换、重命名等行为可能触发启发式规则。发布前用 VirusTotal 自查，并可通过 Microsoft 误报申报页提交样本：`https://www.microsoft.com/en-us/wdsi/filesubmission`。
- **SmartScreen 信誉**：信誉按文件哈希与发布者累积。未签名的程序每次重新编译哈希都会变，信誉积累较慢，这是可接受的已知代价。

### 9.3 将来若要补签名

- 签名步骤只需插入发布流程中「计算 SHA256 之前」的位置，不影响客户端代码。
- 签名必须先于哈希计算，否则哈希失效。

## 十、发布流程

每次发版按以下步骤执行，建议写成脚本：

1. 在 `user/inc/appinfo.h` 升版本号（`APP_VERSION`、`APP_VERSION_NUM`、`APP_BUILD_DATE`）。仅在明确发版时升版，符合 AGENTS.md 规则。
2. 用静态 Qt 编译 release，产物为 `release/dsh_plugin_updater.exe`。
3. 计算 exe 的 `size` 与 `sha256`。
4. 生成 `latest.json`（脚本生成，避免手写出错）。
5. 用私钥签名，得到 `latest.json.sig`。
6. 上传，**先传 exe，后传清单**，保证清单指向的文件已经就位：

   ```bash
   scp dsh_plugin_updater.exe user@<公网IP>:/srv/updates/dsh_plugin_updater/releases/SV01.010/
   scp latest.json latest.json.sig user@<公网IP>:/srv/updates/dsh_plugin_updater/
   ```

7. 验证：用手机网络下载清单并验签，下载 exe 并比对 SHA256。
8. 按 AGENTS.md 要求，在 `docs/README.zh_CN.md` 追加一条更新记录。该文件编入资源，需重新编译后才会在「更新说明」窗口中可见。

## 十一、实施阶段与验收

| 阶段 | 内容 | 验收标准 |
|---|---|---|
| 0 | **端口可达性实测**：安全组与系统防火墙放行端口，用手机网络访问测试；生成签名密钥对 | 手机网络能访问测试页；`openssl dgst` 签名与验证通过 |
| 1 | 服务器部署 Nginx 与目录，放置假清单 | `curl` 能拿到清单，`Cache-Control` 正确 |
| 2 | Qt 5.12.8 静态编译 qtbase，用静态 Qt 编译现有工程 | 单 exe 在空目录中双击运行，界面与动态版一致 |
| 3 | 清单解析、验签、版本比较，按钮与窗口框架 | 假清单能显示「有更新 / 已最新」；篡改清单被拒绝 |
| 4 | 下载、进度条、停滞超时、取消、大小与 SHA256 校验 | 进度正常；中断、取消、篡改 exe 均被正确处理 |
| 5 | 自替换、回滚、启动清理 `.old`；不可写目录兜底 | 实机更新一次成功，版本号变化；Program Files 下能另存到桌面 |
| 6 | 发布脚本，完整演练 SV01.010 → SV01.011 | 全流程无人工改文件 |
| 7 | 异常测试与文档收尾 | 见下方测试清单全部通过 |

**阶段 0 是路线 A 的前提**：端口从手机网络不可达时，路线 A 不成立，需要重新确定方案，不进入阶段 1。

**测试清单**：

- 无网络；服务器 404；请求超时；
- 清单被篡改；清单 `app` 字段错误；清单 `schema` 不认识；
- 远端版本相同 / 更低；
- exe 被篡改（SHA256 不符）；exe 大小不符；
- 下载中途断网；下载中途取消；下载停滞；
- 插件更新任务正在运行时点击替换；
- 程序放在 Program Files 或只读目录，验证桌面另存流程；
- 路径含空格与中文；
- 杀毒软件占用 `.new` 或 exe 时重命名重试。

## 十二、风险与对策

| 风险 | 对策 |
|---|---|
| 阿里云大陆机对非标准端口也拦截 | 阶段 0 用手机网络实测；不通则停止实施，重新评估方案 |
| HTTP 明文传输 | 内容可被看到，但无法被篡改（有签名与哈希）；最坏情况是被拦截导致无法更新 |
| 私钥泄露 | 私钥只存本机并离线备份；泄露后需换密钥并手动安装一次 |
| 替换中途断电或进程被杀 | 保留 `.old`；启动时若发现 exe 缺失而 `.old` 存在，则自动恢复 |
| 端口或公网 IP 变更 | 已分发的老版本只认原地址，变更后无法自动更新；绑定弹性公网 IP，端口一经确定不改 |
| 服务器被运营商或阿里云限制访问 | 监控清单可访问性；必要时发布说明中给出手动下载地址 |
| 杀毒误报 | 发布前 VirusTotal 自查，并向 Microsoft 提交误报申报 |
| 不可写目录用户不会手动替换 | 窗口中给出明确步骤，并在桌面保留新版文件 |

## 十三、待定事项

- 无。路线 A、端口策略与其它决策均已确定。
- 实施前需要确认的具体数值：端口号（示例 8080，以阶段 0 实测结果为准）、阿里云 ECS 公网 IP（用于 `UPDATE_BASE_URL`）。

## 修订记录

- v1：初版方案，列出待确认事项。
- v2：按确认结果定稿，补充 ICP 备案与端口实测路线。
- v3（当前）：确定路线 A（非标准端口 + HTTP，不备案）。调整服务器端口、阶段 0 验收、风险与待定事项。
