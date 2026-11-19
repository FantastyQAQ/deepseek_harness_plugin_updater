# 在线更新方案（Online Update）

> 状态：定稿并已实施（v7 起落地方式改为「发布文件名」方案）
> 适用工程：dsh_plugin_updater（Qt 5.12.8 + MinGW 7.3 32 位 + C++17）
> 目标版本：SV01.010 起

## 〇、已确认决策

| 事项 | 结论 |
|---|---|
| 更新顺序 | 先在更新窗口内下载（显示进度条），校验通过后启动新版本文件并退出旧程序 |
| 服务器 | 中国大陆阿里云 ECS（Linux），采用路线 A：非标准端口 + HTTP，不备案，见第五节 |
| 落地方式 | 新版本按**发布文件名**独立落盘（如 `dsh_plugin_updater_SV01.011.exe`），不改名、不覆盖运行中的 exe，见 6.7 |
| 不可写目录 | 接受兜底：另存到桌面，由用户手动运行 |
| 启动检查 | 不自动检查，仅由用户点击「在线更新」按钮触发 |
| 代码签名 | 本期不做 Authenticode 签名，见第九节 |
| 编译失败 | 不纳入本方案的分发风险范围 |
| 分发形态 | 单 exe：动态编译后用 EVB（Enigma Virtual Box）按 `pack/pack_cfg.evb` 打包，不安装，直接运行 |

## 一、目标与范围

**要做的**

- 主窗口工具栏「关于」按钮右侧新增「在线更新」按钮。
- 点击后弹出「在线更新」窗口，检查服务器上是否有新版本。
- 有新版本时显示版本号与更新说明，用户点击「立即更新」后开始下载，窗口内显示进度条。
- 下载并校验通过后，启动新版本文件，旧程序自动退出。
- 程序放在不可写目录时，提供「另存到桌面、手动运行」的兜底。

**本期不做**

- 启动时自动检查更新。
- Authenticode 代码签名。
- 增量更新、多渠道、版本回退。
- DSH 插件本身的更新（那是 `PluginManager` 已有的功能，与本方案无关）。

## 二、更新顺序说明

需求的字面顺序是「确认 → 关闭旧程序 → 下载」。本方案采用的顺序是：

**确认 → 在更新窗口内下载（进度条）→ 校验通过 → 启动新版 → 旧程序退出**

理由：

- 下载失败（断网、服务器异常）时旧程序仍可用，不会出现程序退出后无法恢复的情况。
- 进度窗口始终在同一个进程内，无需额外的更新助手进程。
- 从用户视角看，仍然是「确认后下载，下载完成后启动新版、旧窗口关闭」。

## 三、关键决策汇总

| 项 | 选择 | 理由 |
|---|---|---|
| 分发形态 | 单 exe，动态编译 + EVB 打包 | 用户已有现成的打包配置，直接运行 |
| 传输协议 | HTTP，使用非标准端口（路线 A） | 阿里云大陆机未备案时 80/443 受限，见第五节 |
| 完整性保证 | 清单 RSA-2048 签名 + exe 的 SHA256 + 大小校验 | HTTP 明文无法保证内容可信，签名是核心 |
| 验签实现 | Windows 自带 BCrypt API | 不引入 OpenSSL，依赖更少 |
| 落地方式 | 新版本按发布文件名独立落盘，校验后直接启动 | 不碰运行中的 exe，不会被文件占用卡住，无需 bat/cmd |
| 服务器 | 阿里云 ECS + Nginx 静态文件托管 | 不需要后端程序，维护成本最低 |
| 不可写目录 | 另存到桌面，用户手动运行 | 已确认的兜底方案 |
| 代码签名 | 不做 | 已确认，见第九节 |
| 触发方式 | 仅手动点击「在线更新」 | 已确认，不做启动自动检查 |

## 四、总体架构

**客户端**（`dsh_plugin_updater.exe`）

- 主窗口工具栏：`[关于] [在线更新] [更新说明] [退出]`
- `OnlineUpdateDialog`：窗口与状态机，串起以下模块
  - `UpdateManifest`：解析 `latest.json`，解析版本号并比较
  - `UpdateVerifier`：RSA 验签（BCrypt）、文件 SHA256
  - `UpdateDownloader`：下载 exe，发出进度信号，支持取消与停滞超时
  - `UpdateApplier`：目录可写性检查、新版本文件命名与路径、启动新版、清理旧版残留

**服务器**（阿里云 ECS，Linux + 宝塔面板）

- Nginx 监听非标准端口 `8080`（已定稿），托管 `/www/wwwroot/qttools_updates/dsh_plugin_updater/`
- 目录中包含 `latest.json`、`latest.json.sig` 和 `releases/<版本>/dsh_plugin_updater_SV01.<NNN>.exe`

**数据流**

```
客户端 ──GET latest.json / latest.json.sig──► 阿里云 ECS（Nginx，非标准端口）
客户端 ◄──────────── 清单 + 签名 ─────────────┘
客户端：验签 → 比较版本 → 用户确认
客户端 ──GET releases/<版本>/dsh_plugin_updater_SV01.<NNN>.exe──► 阿里云 ECS（带进度）
客户端：大小与 SHA256 校验 → 窗口提示 → 启动新版 → 旧程序退出
```

## 五、服务器端部署（阿里云 ECS，路线 A）

### 5.1 端口与备案说明

阿里云中国大陆服务器对通过 **80 / 443 端口**对外提供的网站有备案检查，未备案的服务器这两个端口会被拦截。

路线 A 的做法是：**不使用 80/443，改用非标准端口提供 HTTP 服务，不做 ICP 备案。**

实施要求：

- 端口选择：**已定稿 8080**（可用手机网络实测确认，若被拦截可换 8081/9000 等高位的非常用端口，但发布后不可再改）。
- **阶段 0 必须实测**：用手机 4G/5G 网络（与服务器所在网络不同）访问 `http://39.107.30.119:8080/dsh_plugin_updater/latest.json`，确认外网可达。
- 实测不通时，路线 A 不成立，需要回到方案决策（转路线 B：域名 + 备案）。

### 5.2 云控制台与系统准备

- **ECS 安全组**：放行所选端口（已确定 TCP 8080）。**必须手工选「自定义 TCP」**——右侧「Web HTTP 流量访问」快捷配置会把端口锁死为 80，与路线 A 冲突。
- **访问来源**：填 `0.0.0.0/0`，在线更新面向所有使用者，不能限定来源 IP。
- **宝塔防火墙**：面板「安全」页同步放行 8080。它与安全组是两层独立限制，只开一层表现都是「连接超时」。
- **系统防火墙（firewalld / ufw）：实测漏点，必须单独放行**。本机实际启用了 firewalld，而它的放行列表里原本没有 8080：

  ```bash
  firewall-cmd --permanent --add-port=8080/tcp
  firewall-cmd --reload
  firewall-cmd --query-port=8080/tcp     # 应返回 yes
  ```

  > 注意 `iptables -S INPUT` 只显示 `-P INPUT ACCEPT` 时并不能说明没拦截：firewalld 走的是自己的链（`IN_public` 等），必须用 `firewall-cmd --list-ports` 核对。

- **SSH**：仅用于上传文件，建议密钥登录，禁用密码登录。

> 实测提醒：安全组里 22 端口当前对 `0.0.0.0/0` 开放，存在暴力破解风险，建议收敛为固定来源 IP。

### 5.3 服务器环境：宝塔面板（实测确认）

本项目的目标服务器**已安装宝塔面板并托管着其它站点**，因此部署方式与「裸装 Nginx」不同：

| 项目 | 实际值 |
|---|---|
| 面板类型 | 宝塔 Linux 面板（Nginx 1.30.6） |
| 站点类型 | **HTML 项目**（纯静态），不可选「反向代理」 |
| 站点根目录 | `/www/wwwroot/qttools_updates` |
| 站点配置 | `/www/server/panel/vhost/nginx/html_39.107.30.119.conf` |
| 站点扩展配置 | `/www/server/panel/vhost/nginx/extension/39.107.30.119/`（宝塔自动生成，仅含 `access_log syslog:...` 流量统计，无害） |
| Nginx 可执行 | `/www/server/nginx/sbin/nginx`（不在 PATH 中） |
| Nginx 运行用户 | `www` |
| 错误日志 | `/www/wwwlogs/39.107.30.119.error.log` |

**关键约束**：

- 不安装、不升级、不卸载 Nginx —— 宝塔管的 Nginx 升级会覆盖自定义配置；
- 只在站点配置文件里改动，不碰 `/www/server/panel/vhost/nginx/` 下其它站点；
- 不使用 `default_server`，避免与同端口已有站点抢默认虚拟主机；
- 保存配置后由宝塔自动执行 `nginx -t` 并 reload，校验失败会拒绝保存，现有站点不受影响。

> `scripts/setup_server.sh` 是「裸装 Nginx」版本（写 `/etc/nginx/conf.d/`），**在宝塔环境不适用**，脚本已内置检测：发现 `/www/server/panel` 会直接报错退出。宝塔环境请按 5.4、5.5 的界面步骤配置。

### 5.4 目录结构

```
/www/wwwroot/qttools_updates/                 # 站点根，URL 中不可见
├── dsh_plugin_updater/                       # -> /dsh_plugin_updater/...
│   ├── latest.json                          # 清单，每次检查都读取最新内容
│   ├── latest.json.sig                      # 清单签名
│   └── releases/
│       ├── SV01.009/dsh_plugin_updater_SV01.009.exe
│       └── SV01.011/dsh_plugin_updater_SV01.011.exe  # 发布后不再修改
├── github_pull_tool/
├── opencode_proxy_tool/
├── esp32_s3_tool/
├── vscode_remote_fixer/
└── protocol_analyzer/
```

每个应用一个一级子目录，目录名即 `APP_ID`，结构完全相同；新增应用只建目录，不改 Nginx。

创建并修正属主（Nginx 以 `www` 用户运行，属主与读权限不对会返回 403）：

```bash
sudo mkdir -p /www/wwwroot/qttools_updates/dsh_plugin_updater/releases
sudo chown -R www:www /www/wwwroot/qttools_updates
sudo chmod -R a+rX /www/wwwroot/qttools_updates
```

> 上传注意：`/www` 属性为 `drwx------`（属主 `www`），**普通账号（如 `lch`）用 SFTP/Xftp 无法进入**。三种可行方式：① 用宝塔「文件」管理器上传（推荐，绕开权限问题）；② 用 root 登录 SFTP；③ 先传到 `/home/<user>`，再 `sudo cp` 到目标目录并 `chown`。

### 5.5 Nginx 配置（宝塔界面操作）

**步骤**：

1. 面板 → **网站** → 顶部切到 **HTML项目** → **添加项目**
2. 名称 `qttools_updates`、域名填公网 IP `39.107.30.119`、根目录 `/www/wwwroot/qttools_updates`
3. 不创建 FTP、不创建数据库、PHP 版本选「纯静态」
4. 建好后 → 站点 **设置** → **配置文件**，按下面修改

把 `listen 80;` 改为 `listen 8080;`（若为 `listen 80 default_server;`，**去掉 `default_server`**），并在 `location` 块之前加入两段缓存规则：

```nginx
server {
    listen 8080;
    server_name 39.107.30.119;

    root /www/wwwroot/qttools_updates;
    autoindex off;

    # 清单必须每次都取最新，禁止缓存（匹配任意 APP_ID 子目录）
    location ~ ^/[a-z0-9_]+/latest\.json(\.sig)?$ {
        add_header Cache-Control "no-cache";
    }

    # 版本目录一经发布不再修改，可以长期缓存
    location ~ ^/[a-z0-9_]+/releases/ {
        add_header Cache-Control "public, max-age=31536000";
    }

    # 其它路径一律 404
    location / {
        return 404;
    }
}
```

保存时宝塔会自行执行 `nginx -t` 并 reload；校验失败会拒绝保存，现有站点不受影响。

**判断配置是否正确的关键**：HTML 项目的配置里只有 `root` 与 `location`，**绝不能出现 `proxy_pass`**。出现 `proxy_pass` 说明建站时选成了「反向代理」，此时 8080 会返回 **502 Bad Gateway**（找不到上游），必须删掉重建为 HTML 项目。

Nginx 对静态文件会返回 `Content-Length`，客户端据此计算下载进度百分比。

### 5.6 清单 latest.json

```json
{
  "schema": 1,
  "app": "dsh_plugin_updater",
  "version": "SV01.010",
  "size": 15728640,
  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "url": "releases/SV01.010/dsh_plugin_updater_SV01.010.exe",
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
# 服务器本地先验证静态站点本身
curl -I http://127.0.0.1:8080/dsh_plugin_updater/latest.json
curl -s  http://127.0.0.1:8080/dsh_plugin_updater/latest.json
```

返回 200 且 `Cache-Control: no-cache` 存在即通过。Windows 下可用 PowerShell 的 `Invoke-WebRequest` 代替。**外网验证请使用手机网络，不要只在服务器所在网络内测试。**

**排查用的一行诊断**（在服务器上执行）：

```bash
echo "=== 8080 监听 ==="; ss -lntp | grep 8080
echo "=== 本地直连 ==="; curl -sS -I --max-time 5 http://127.0.0.1:8080/
echo "=== 站点配置 ==="; grep -rn -A 15 "listen 8080" /www/server/panel/vhost/nginx/ 2>/dev/null
echo "=== 错误日志 ==="; tail -20 /www/wwwlogs/39.107.30.119.error.log
```

**判断外网可达性的正确方法（实测教训）**：

应以「服务器本地 `127.0.0.1` 测试」+「手机流量测试」两个来源为准。**不要**用开发者办公网络的结果判断——实测中，公司网络出口代理对不可达端口会返回 `502 Bad Gateway` 且**不带 `Server` 头**、`Content-Length: 0`，极易被误判为「Nginx 反代配错」。

同理，在服务器上 `curl` 自己的**公网 IP** 也可能超时（ECS 公网 IP 为 NAT 映射，多数情况不支持 hairpin 回环），这属正常现象，不能据此判定端口未放行。访问自己的**私网 IP** 则会走 loopback，即使防火墙拦截也能返回 200，同样不能用于判断。

状态码对照：

| 现象 | 含义 | 处理 |
|---|---|---|
| **502 Bad Gateway**（无 `Server` 头） | 多半是本地网络代理的假响应，不是服务器问题 | 改用服务器本地 + 手机流量复测 |
| **502 Bad Gateway**（有 `Server: nginx`） | 站点被配成反向代理，找不到上游 | 重建为 HTML 项目，确认配置无 `proxy_pass` |
| **403 Forbidden** | 属主/读权限不对 | `chown -R www:www` + `chmod -R a+rX` |
| **404 Not Found** | 文件没上传，或 `root` 指向别的目录 | 核对 5.4 的目录结构 |
| **手机端 ERR_ADDRESS_UNREACHABLE / 超时** | 安全组、firewalld 或宝塔防火墙未放行 | 三层逐一核对，重点看 `firewall-cmd --list-ports` |
| 运营商提示页 | 端口被运营商拦截 | 换高位端口重测 |

### 5.8 端口与地址的长期约定

- 已分发的客户端内置的是 `IP + 端口`，**端口一经确定不要更改**，否则老版本无法获取更新。
- 公网 IP 如果变化（如 ECS 重新分配），同样会导致老版本失效。建议为 ECS 绑定弹性公网 IP（EIP），保持地址稳定。
- 将来若要切换到域名（需要备案），需要重新评估，届时另行出方案。

### 5.9 线上地址（已定稿）

| 项目 | 值 |
|---|---|
| 公网 IP | `39.107.30.119` |
| 端口 | `8080` |
| 协议 | HTTP（路线 A，不备案） |
| `UPDATE_BASE_URL` | `http://39.107.30.119:8080/dsh_plugin_updater/` |
| 清单地址 | `http://39.107.30.119:8080/dsh_plugin_updater/latest.json` |

**已写入** `user/inc/update_config.h`，随客户端编译。该地址一经分发不可再改。

## 六、客户端设计

### 6.1 配置与常量

`user/inc/update_config.h`（已实现），集中放置可调参数：

```cpp
// 应用标识：清单里的 app 字段必须与之一致，防止拿错清单
#define UPDATE_APP_ID            "dsh_plugin_updater"
// 更新源基址（末尾必须带斜杠，清单与版本目录都相对它解析）
#define UPDATE_BASE_URL          "http://39.107.30.119:8080/dsh_plugin_updater/"
// 检查清单的网络超时（毫秒）
#define UPDATE_CHECK_TIMEOUT_MS  10000
// 下载过程中连续无数据的停滞超时（毫秒），Qt 5.12 没有 setTransferTimeout，需自行实现
#define UPDATE_STALL_TIMEOUT_MS  30000
```

RSA 公钥（模数的十六进制与指数）放在 `user/src/update_verifier.cpp` 的静态数组中，不放入头文件的宏里。

版本号不新增宏，直接解析现有的 `APP_VERSION`（`SV01.009`）末尾数字。这样仍然只有一个版本来源，符合 AGENTS.md 中「三个宏同步」的规则。

### 6.2 界面

**主工具栏**：在「关于」之后插入「在线更新」按钮，颜色使用橙色系，与现有按钮风格一致（参照 `main_window.cpp` 中 `m_aboutBtn` 的写法）：

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
- 下载、校验或启动新版进行中，窗口不允许通过关闭按钮退出（`closeEvent` 中拦截）。

### 6.3 状态机

| 状态 | 进入条件 | 界面表现 |
|---|---|---|
| `Idle` | 打开窗口 | 显示当前版本，按钮「检查更新」可用 |
| `Checking` | 点击检查更新 | 进度条忙碌态，状态「正在检查…」 |
| `UpToDate` | 远端版本 ≤ 本地版本 | 状态「已是最新版本」 |
| `Available` | 远端版本 > 本地版本，且清单验签通过 | 显示新版本与更新说明，「立即更新」可用 |
| `Downloading` | 点击立即更新 | 进度条 0–100，显示已下载量与速度 |
| `Verifying` | 下载完成 | 进度条忙碌态，状态「正在校验…」 |
| `Applying` | 校验通过且目录可写 | 状态「正在启动新版本…」，启动后退出 |
| `ManualInstall` | 校验通过但目录不可写 | 进入兜底流程，见 6.8 节 |
| `Error` | 任何步骤失败 | 红色错误信息，按钮恢复为「检查更新」 |

### 6.4 文件清单

| 文件 | 职责 |
|---|---|
| `user/inc/update_config.h` | 更新源地址与超时参数（单行注释） |
| `user/inc/update_manifest.h` / `user/src/update_manifest.cpp` | 解析 `latest.json`，解析版本号并比较 |
| `user/inc/update_verifier.h` / `user/src/update_verifier.cpp` | RSA 验签（BCrypt）、文件 SHA256 计算 |
| `user/inc/update_downloader.h` / `user/src/update_downloader.cpp` | 基于 QNetworkAccessManager 下载，发出进度，支持取消与停滞超时 |
| `user/inc/update_applier.h` / `user/src/update_applier.cpp` | 目录可写性检查、新版本文件名与路径、启动新版、清理旧版 `.old` / `.new` 残留 |
| `user/inc/online_update_dialog.h` / `user/src/online_update_dialog.cpp` | 窗口与状态机，串起以上模块 |
| `user/src/main_window.cpp` | 新增按钮与 `on_online_update()` 槽 |
| `user/src/main.cpp` | 启动时调用 `UpdateApplier::cleanup_leftovers()` |
| `dsh_plugin_updater.pro` | 增加 `QT += network`、`LIBS += -lbcrypt`（平台与图标插件已由 EVB 配置打包，无需 `QTPLUGIN`） |
| `pack/app_32/Qt5Network.dll`、`pack/pack_cfg.evb` | 已补入 Qt5Network.dll 并写入 EVB 配置（见第七节） |

源文件函数注释使用 Doxygen 风格，头文件只写单行说明，符合 AGENTS.md 约定。

### 6.5 检查流程

1. 请求 `UPDATE_BASE_URL + "latest.json"`，超时 `UPDATE_CHECK_TIMEOUT_MS`。缓存策略设为 `AlwaysNetwork`，避免读到旧缓存。
2. 请求 `latest.json.sig`。
3. 用内置公钥验签 `latest.json` 的**原始字节**。失败则直接报错「更新源校验失败」，不解析内容。
4. 解析 JSON，检查 `schema == 1` 且 `app == "dsh_plugin_updater"`。
5. 比较版本：远端末尾数字 > 本地末尾数字才提示更新。远端更低或相同则不做任何事，**不做降级**。
6. 用 `QUrl(UPDATE_BASE_URL).resolved(QUrl(manifest.url))` 得到 exe 的完整地址。

### 6.6 下载与校验

- **落地路径**：下载到 `<exe所在目录>\<exe名去掉版本段>_<新版本号>.exe`，即与服务器发布文件名完全一致（如 `dsh_plugin_updater_SV01.011.exe`），由 `UpdateApplier::get_publish_path()` 生成；版本号先做 `[A-Za-z0-9._-]` 白名单校验，防止清单里塞路径分隔符。文件名里的版本段一律来自清单，不写死。
- **可写性预检**：开始下载前，先尝试在 exe 所在目录创建探针文件。失败则进入 6.8 节的兜底流程（改下到系统临时目录），并且不浪费流量去下载。
- **进度**：连接 `QNetworkReply::downloadProgress(received, total)`。若 `total == -1`，进度条切换为忙碌态。
- **速度**：每 500 ms 采样一次，用字节差除以时间差计算。
- **停滞超时**：Qt 5.12 没有 `setTransferTimeout`（5.15 才有），需要用单次 `QTimer`，每收到数据重置，超过 `UPDATE_STALL_TIMEOUT_MS` 则 `reply->abort()`。
- **重定向**：`setAttribute(QNetworkRequest::FollowRedirectsAttribute, true)`。
- **取消**：`reply->abort()` 后删除已下载的文件。
- **并发**：同一时间只允许一个下载任务。
- **完成后校验**：
  - 实际字节数必须等于 `manifest.size`；
  - SHA256 必须等于 `manifest.sha256`（用 `QCryptographicHash` 分块计算，避免整个文件读入内存）。
  - 任一不符则删除该文件，报错。

### 6.7 落地与启动（发布文件名方案）

> **2026-11-18 修订**：原方案是「把运行中的 exe 改名成 `.old`，再把 `.new` 改名回原名」。实测在一次真实升级中，第一步重命名就被占用挡下（`C:\sw\tool` 下的 SV01.010 升级 SV01.011），只留下一个 `dsh_plugin_updater_SV01.010.exe.new` 和一句「无法重命名当前程序」。运行中的 exe 本身允许改名，但只要杀毒软件、资源管理器缩略图、云同步或备份工具短暂持有句柄就会失败，而失败后除了一句报错没有任何可用产物。**现改为下面的方案，不再做任何改名。**

新版本是**一个独立完整程序**，按发布文件名直接落到 exe 同目录：

```
E:\tools\
├── dsh_plugin_updater_SV01.010.exe            # 正在运行的旧版
└── dsh_plugin_updater_SV01.011.exe            # 刚下载并校验通过的新版
```

设当前 exe 路径为 `E:\tools\dsh_plugin_updater_SV01.010.exe`（通过 `QCoreApplication::applicationFilePath()` 获取，不写死文件名）。

> **已实测（2026-11-18）**：程序经 EVB 打包运行，`pack_cfg.evb` 中 `MapExecutableWithTemporaryFile` 为 `True`，但 `applicationFilePath()` 仍返回**原始打包 exe 的真实路径**（详见 7.5）。因此按真实目录落盘没有问题。
>
> 代码中仍保留兜底：若路径判定落在系统临时目录，则改走「另存到桌面」，绝不往临时目录旁边的位置写新版本。

1. 下载与校验通过，新版本文件已在 exe 同目录，名字与清单里的版本号一致。
2. 检查 `PluginManager` 的 `m_activeProc` 与 `m_queryProc` 是否在运行。若在运行，提示「插件更新任务正在进行，请等待其完成后再更新程序」，停止后续步骤（新版本文件保留，稍后再点「立即更新」会覆盖重下）。
3. `QProcess::startDetached(新版本路径, {}, exeDir)` 启动新版，工作目录与手动双击一致。
4. 启动成功：状态栏提示「已启动新版本 SV01.0NN」，`qApp->quit()` 退出旧进程。
5. 启动失败：报错并给出新版本文件的完整路径，旧程序继续运行（新文件不会被删除，用户可以手动运行它）。
6. **新版启动时清理**（`main.cpp` 中调用 `cleanup_leftovers()`）：
   - 若存在老方案留下的 `<exe>.old`：正式 exe 在则删除，不在则把 `.old` 改回原名（恢复旧版）；
   - 若存在老方案留下的 `.new`（同目录或系统临时目录）：直接删除。

**为什么不再改名**

| 对比项 | 旧方案（改名替换） | 现方案（发布文件名落地） |
|---|---|---|
| 是否碰运行中的 exe | 是，两次重命名 | 否，一次都不碰 |
| 失败风险 | 任何持句柄的进程都可能导致改名失败 | 只需能写入新文件，与旧 exe 无关 |
| 失败后的产物 | 一个 `.new` 残骸，用户无从下手 | 一个可直接双击运行的正式版本文件 |
| 旧版本处置 | 被换成 `.old` 后删除 | 留在原目录，由用户自行删除 |
| 进程切换 | 同一路径被替换 | 两个版本并存，新进程启动后旧进程退出 |

**代价**：目录里会逐渐堆积历史版本（与用户已有的 `_SV01.009.exe`、`_SV01.010.exe` 同一形态）。本期不做自动清理；如需清理，可在 `cleanup_leftovers()` 中删除「同前缀且版本号小于当前」的 exe，属于后续优化项。

**未采用的方案**：「先关闭旧程序再下载」需要增加更新助手进程，与第二节确定的顺序不符，本期不实现。

### 6.8 不可写目录的兜底（已确认）

程序可能放在 `C:\Program Files`、只读介质等不可写目录。此时采用「另存到桌面、手动运行」：

1. 预检发现 exe 所在目录不可写（或 exe 路径落在系统临时目录）。
2. 新版本先下载到系统临时目录，同样经过大小与 SHA256 校验。
3. 校验通过后复制到桌面，文件名保持发布时的名字：`<桌面>\dsh_plugin_updater_SV01.0NN.exe`，桌面路径用 `QStandardPaths::DesktopLocation` 获取。
4. 更新窗口中提示：「当前程序所在目录不可写（可能是 Program Files 或只读介质）。新版本已保存到：<桌面路径>，请关闭本程序后直接运行这个文件。」
5. 旧程序**不关闭**，继续可用。

### 6.9 异常处理一览

| 场景 | 处理 |
|---|---|
| 无网络 / 服务器不可达 | 错误提示，允许重试 |
| 清单签名无效 | 拒绝，提示「更新源校验失败」 |
| 清单 `app` / `schema` 不匹配 | 拒绝，提示「更新清单格式不符」 |
| 清单版本号含非法字符 | 拒绝，提示「版本号不合法，无法确定新版本文件名」 |
| 远端版本 ≤ 本地版本 | 显示「已是最新版本」 |
| 下载中断或停滞超时 | 报错，删除已下载的文件 |
| 用户取消下载 | 删除已下载的文件，回到 Idle |
| 实际大小或 SHA256 不符 | 删除已下载的文件，报错 |
| 目录不可写 | 进入 6.8 兜底流程 |
| 插件更新任务正在运行 | 阻止启动新版，提示等待 |
| 同名新版本文件被占用，写不进去 | 报错「无法写入新版本文件（同名文件可能正被占用）」，重试即可 |
| 新版启动失败 | 报错并给出新版本文件路径，旧程序继续运行 |
| 老版本残留 `.old` 且正式 exe 丢失 | 启动时 `cleanup_leftovers()` 把 `.old` 改回原名 |

## 七、动态编译与 EVB 打包（单 exe 的前提）

### 7.1 为什么需要打包

动态链接的 exe 运行时依赖 `Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll`、`Qt5Network.dll`、`platforms\qwindows.dll`、`imageformats\qico.dll` 等 Qt 运行库，以及 MinGW 运行库（`libgcc_s_dw2-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`）。直接分发需要整个目录。

本方案不做静态编译，沿用已有的 EVB（Enigma Virtual Box）打包流程：把动态编译出的 exe 与 `pack/app_32/` 下的运行库封装为单个 exe，用户双击即可运行。

### 7.2 所需运行库

运行库统一放在 `pack/app_32/`，`pack/pack_cfg.evb` 已列出全部条目。本方案新增的依赖是 **Qt5Network.dll**（供在线更新的 `QNetworkAccessManager` 使用），已拷入 `pack/app_32/` 并写入 EVB 配置。

| 类别 | 文件 |
|---|---|
| Qt 核心 | `Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll`、`Qt5Network.dll`、`Qt5Svg.dll` |
| 平台与样式 | `platforms\qwindows.dll`、`styles\qwindowsvistastyle.dll` |
| 图标与图片 | `imageformats\*.dll`、`iconengines\qsvgicon.dll` |
| MinGW 运行库 | `libgcc_s_dw2-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll` |
| 其它 | `libEGL.dll`、`libGLESV2.dll`、`opengl32sw.dll`、`D3Dcompiler_47.dll`、`translations\*.qm` |

运行库版本必须与编译用的 Qt 5.12.8（mingw73_32）一致。`Qt5Network.dll` 取自 `D:\sw\Qt5.12.8\5.12.8\mingw73_32\bin`，与 `Qt5Core.dll` 同为 5.12.8.0。

### 7.3 编译与打包步骤

1. 在**纯 cmd 环境**下执行 qmake 与 make（不要在 Git Bash / MSYS2 中执行，原因见 AGENTS.md 中关于 `sh.exe` 的说明）。工具链与现有构建一致，使用 32 位 mingw730 与 Qt 5.12.8 mingw73_32：

   ```cmd
   set PATH=D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin;D:\SW\Qt5.12.8\Tools\mingw730_32\bin;C:\Windows\system32;C:\Windows
   D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin\qmake.exe dsh_plugin_updater.pro -spec win32-g++
   mingw32-make.exe -j4
   ```

2. 编译产物为 `release/dsh_plugin_updater.exe`（动态链接，不需要额外的 Qt 静态库）。
3. 用 EVB 打开 `pack/pack_cfg.evb` 执行打包，或用**命令行打包**（实测可用，便于脚本化）：

   ```cmd
   "D:\SW\Enigma Virtual Box\enigmavbconsole.exe" pack\pack_cfg.evb
   ```

   配置中的输入为 `release/dsh_plugin_updater.exe`，输出由 `<OutputFile>` 指定（一行为 `pack/dsh_plugin_updater_SV01.<NNN>.exe`）。这个打包产物才是要分发和上传的单文件。

   > 注意：`pack_cfg.evb` 里的输出文件名带版本号（如 `SV01.011`），**版本号改动后要同步改这里**，否则会覆盖上一个版本的打包产物导致混淆。`publish.ps1` 会把该文件名原样写进清单的 `url` 字段，因此上传时**不需要改名**。

**关于 HTTP 与 OpenSSL**：当前方案使用 HTTP 与 BCrypt 验签，不需要 OpenSSL，工程中不引入 OpenSSL 相关依赖。

### 7.4 工程文件改动

`dsh_plugin_updater.pro` 增加：

```
QT      += core gui widgets network
LIBS    += -lbcrypt           # Windows CNG，用于 RSA 验签
```

平台插件 `qwindows.dll`、图标插件 `qico.dll` 已由 EVB 配置打包，不需要 `QTPLUGIN` 或 `Q_IMPORT_PLUGIN`。

### 7.5 验证

- 将打包产物复制到一个只有它自己的空目录，在没有安装 Qt 的 Windows 机器上双击运行，界面与已发布版一致即通过。
- 同时确认在线更新窗口能正常发起网络请求（即 `Qt5Network.dll` 已被正确打包）。

**实测记录（2026-11-18）**：把 `pack/dsh_plugin_updater_SV01.011.exe` 复制到空目录并在旁放置一个老方案的 `.new` 文件后启动，程序启动时的 `cleanup_leftovers()` 正确删除了该 `.new`，且进程映像路径显示为真实目录：

```
Path = C:\Users\lch_w\AppData\Local\Temp\opencode\evb_test\dsh_plugin_updater.exe
.new 已删除 → applicationFilePath() 指向真实目录，新版本可就地落盘
```

结论：尽管 `MapExecutableWithTemporaryFile` 为 `True`，`applicationFilePath()` 仍返回真实路径，**新版本按发布文件名落到同目录的方案成立**，不会退化为「另存到桌面」。

### 7.6 风险

- EVB 解包的运行库会在临时目录中留存（`DeleteExtractedOnExit` 为 False），属于现有配置的已知行为，不影响更新。
- 打包后的 exe 启动时需解包运行库，首次启动会比裸 exe 稍慢。
- ~~`applicationFilePath()` 在 EVB 下的返回值需实测确认~~ —— **已实测排除**，见 7.5。
- `pack_cfg.evb` 的输出文件名带版本号，升版时必须同步修改，否则会覆盖旧产物。

## 八、清单签名（本方案必做）

本期不做 Authenticode 代码签名，但**清单签名必须做**，它是更新通道安全的核心。

### 8.1 生成密钥对（只做一次）

在本地 Windows 上执行，可使用 Git 自带的 `openssl.exe`（这里只用于生成密钥，不涉及 qmake）：

```bash
openssl genrsa -out update_sign_private.pem 2048
openssl rsa -in update_sign_private.pem -pubout -out update_sign_public.pem
openssl rsa -pubin -in update_sign_public.pem -noout -modulus
```

最后一条输出 `Modulus=<十六进制>`，把它填入 `user/src/update_verifier.cpp` 的公钥数组。指数固定为 65537（`0x010001`）。

**私钥保管**：

- **实际存放位置**：工程根目录下的 `update_keys/`（相对路径），发布脚本默认使用。
- 私钥 `update_keys/update_sign_private.pem` 不放服务器；整个目录已被 `.gitignore` 排除。
- **必须备份到离线介质**（U 盘、加密压缩包）：私钥丢失将无法再发布更新。
- 公钥 `update_keys/update_sign_public.pem` 与之同目录，供发布脚本自检签名用；其模数已硬编码在 `user/src/update_verifier.cpp`。
- 私钥泄露后，攻击者可以推送恶意更新。需要更换密钥对，并发布一个新公钥的版本，老版本无法自动升级，需要手动安装。

### 8.2 签名清单

```bash
# 已封装为 scripts/publish.ps1，手工等价命令如下（在工程根目录执行）
openssl dgst -sha256 -sign update_keys/update_sign_private.pem -out publish_info/latest.json.sig publish_info/latest.json
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
- **杀毒误报**：更新时写入新的 exe 文件可能触发启发式规则。发布前用 VirusTotal 自查，并可通过 Microsoft 误报申报页提交样本：`https://www.microsoft.com/en-us/wdsi/filesubmission`。
- **SmartScreen 信誉**：信誉按文件哈希与发布者累积。未签名的程序每次重新编译哈希都会变，信誉积累较慢，这是可接受的已知代价。

### 9.3 将来若要补签名

- 签名步骤只需插入发布流程中「计算 SHA256 之前」的位置，不影响客户端代码。
- 签名必须先于哈希计算，否则哈希失效。

## 十、发布流程

每次发版按以下步骤执行（第 3～5 步已脚本化，见 `scripts/publish.ps1`）：

1. 在 `user/inc/app_info.h` 升版本号（`APP_VERSION`、`APP_VERSION_NUM`、`APP_BUILD_DATE`）。仅在明确发版时升版，符合 AGENTS.md 规则。
2. 动态编译 release，再用 EVB 按 `pack/pack_cfg.evb` 打包，得到 `pack/dsh_plugin_updater_boxed.exe`。
3. 计算**打包后 exe** 的 `size` 与 `sha256`（脚本第 1 步）。
4. 生成 `latest.json`（脚本生成，避免手写出错；会校验版本号与 `app_info.h` 一致）。
5. 用私钥签名得到 `latest.json.sig`，并用配对公钥自检一次（失败直接中止）。
6. 上传，**先传 exe，后传清单**，保证清单指向的文件已经就位：

   ```bash
   # 服务器上的文件名与本地打包产物一致，无需改名
   scp pack/dsh_plugin_updater_SV01.011.exe root@39.107.30.119:/www/wwwroot/qttools_updates/dsh_plugin_updater/releases/SV01.011/dsh_plugin_updater_SV01.011.exe
   scp publish_info/latest.json publish_info/latest.json.sig root@39.107.30.119:/www/wwwroot/qttools_updates/dsh_plugin_updater/
   # 上传后修正属主，否则 Nginx（www 用户）读不到会返回 403
   ssh root@39.107.30.119 "chown -R www:www /www/wwwroot/qttools_updates && chmod -R a+rX /www/wwwroot/qttools_updates"
   ```

   > 目录结构与 `user@` 到 `root@`、`/srv/updates` 到 `/www/wwwroot/qttools_updates` 的差异，来自目标服务器为宝塔环境（见 5.3）。站点根目录名（`qttools_updates`）不出现在 URL 里，漏建 `<APP_ID>` 这一层会直接 404。

7. 验证：用手机网络下载清单并验签，下载 exe 并比对 SHA256。
8. 按 AGENTS.md 要求，在 `docs/README.zh_CN.md` 追加一条更新记录。该文件编入资源，需重新编译后才会在「更新说明」窗口中可见。

## 十一、实施阶段与验收

| 阶段 | 内容 | 验收标准 | 状态 |
|---|---|---|---|
| 0 | **端口可达性实测**：安全组 + firewalld 双层放行，用手机网络访问测试；生成签名密钥对 | 手机网络能访问；`openssl dgst` 签名验证通过 | **已完成**：密钥已生成；8080 从外网可达（`Server: nginx` 正常响应） |
| 1 | 服务器部署 Nginx 与目录，放置假清单 | `curl` 能拿到清单，`Cache-Control` 正确 | **进行中**：站点与目录已就绪，缓存规则与正式文件待传 |
| 2 | 动态编译现有工程（补 `network`），用 EVB 按 `pack_cfg.evb` 打包 | 打包后的单 exe 在空目录中双击运行，界面一致，能发起网络请求 | **已完成**：`pack/dsh_plugin_updater_SV01.011.exe`（22,580,224 字节） |
| 3 | 清单解析、验签、版本比较，按钮与窗口框架 | 假清单能显示「有更新 / 已最新」；篡改清单被拒绝 | **已完成并实测** |
| 4 | 下载、进度条、停滞超时、取消、大小与 SHA256 校验 | 进度正常；中断、取消、篡改 exe 均被正确处理 | **已完成并实测** |
| 5 | 新版本按发布文件名落地并启动；不可写目录兜底 | 实机更新一次成功（新版本文件出现在同目录并启动）；Program Files 下能另存到桌面 | 落地与启动已实现；改名方案在实机升级中被占用挡下，已据此改为现方案 |
| 6 | 发布脚本，完整演练 SV01.011 → SV01.012 | 全流程无人工改文件 | 脚本已完成；完整演练待做 |
| 7 | 异常测试与文档收尾 | 见下方测试清单全部通过 | 待做 |

**阶段 0 是路线 A 的前提**：端口从手机网络不可达时，路线 A 不成立，需要重新确定方案。

**测试清单**：

- 无网络；服务器 404；请求超时；
- 清单被篡改；清单 `app` 字段错误；清单 `schema` 不认识；
- 远端版本相同 / 更低；
- exe 被篡改（SHA256 不符）；exe 大小不符；
- 下载中途断网；下载中途取消；下载停滞；
- 插件更新任务正在运行时点击「立即更新」；
- 程序放在 Program Files 或只读目录，验证桌面另存流程；
- 路径含空格与中文；
- 同版本号的新文件已存在且被占用时，验证报错文案。

## 十二、风险与对策

| 风险 | 对策 |
|---|---|
| 阿里云大陆机对非标准端口也拦截 | 阶段 0 用手机网络实测；不通则停止实施，重新评估方案 |
| HTTP 明文传输 | 内容可被看到，但无法被篡改（有签名与哈希）；最坏情况是被拦截导致无法更新 |
| 私钥泄露 | 私钥只存本机并离线备份；泄露后需换密钥并手动安装一次 |
| 更新中途断电或进程被杀 | 旧版本始终完整可用；新版本文件残缺时校验不通过会被删除，重新下载即可 |
| EVB 下 `applicationFilePath()` 指向临时文件，新版本落到错误位置 | **已实测排除**：EVB 打包后 `applicationFilePath()` 返回真实目录。代码仍保留「临时目录→另存桌面」兜底作为保险 |
| 端口或公网 IP 变更 | 已分发的老版本只认原地址，变更后无法自动更新；绑定弹性公网 IP，端口一经确定不改 |
| 服务器被运营商或阿里云限制访问 | 监控清单可访问性；必要时发布说明中给出手动下载地址 |
| 杀毒误报 | 发布前 VirusTotal 自查，并向 Microsoft 提交误报申报 |
| 不可写目录用户不会手动运行新版 | 窗口中给出明确步骤，并在桌面保留新版文件 |
| 历史版本文件在原目录堆积 | 已知代价，用户可自行删除；后续可在启动清理中删除旧版同名 exe |
| 宝塔环境误用裸装脚本改动现有站点 | 已明确区分：宝塔环境按 5.3～5.5 界面操作，`setup_server.sh` 不适用；脚本仅新增独立配置文件、reload 而非 restart、失败自动回滚 |
| 站点建错类型导致 502 | 部署后用 `curl -I http://127.0.0.1:8080/` 自测；502 即检查配置里有无 `proxy_pass` |

## 十三、待定事项

- 路线 A、端口策略、公网 IP 与其它决策均已确定，无待定项。
- 服务器 8080 已从外网可达（阿里云安全组 + firewalld 双层放行后验证通过）。

## 修订记录

- v1：初版方案，列出待确认事项。
- v2：按确认结果定稿，补充 ICP 备案与端口实测路线。
- v3：确定路线 A（非标准端口 + HTTP，不备案）。调整服务器端口、阶段 0 验收、风险与待定事项。
- v4：分发改为动态编译 + EVB 打包（沿用 `pack/pack_cfg.evb`），删除静态编译相关内容。补入 Qt5Network.dll 并写入 EVB 配置。清单 size/sha256 改为对打包后的 exe 计算。
- v5：按实际部署环境改写。服务器为**宝塔面板**（非裸装 Nginx），站点类型 HTML 项目，根目录 `/www/wwwroot/qttools_updates`，Nginx 以 `www` 用户运行；补入目录权限、上传方式、502/403/404 排查对照表；地址定稿为 `http://39.107.30.119:8080/dsh_plugin_updater/`；按已完成的实测更新实施阶段状态与风险表。
- v6：站点根目录由 `dsh_updates` 改为 `qttools_updates`，用于托管多个 Qt 工具的更新包；缓存规则改为 `[a-z0-9_]+` 通配，新增应用无需改 Nginx。补充两处 404 根因（漏建 `<APP_ID>` 子目录、exe 文件名带版本号）。
- v7（当前）：落地方式由「重命名旧 exe 替换」改为「新版本按发布文件名独立落盘并直接启动」。起因是实测中运行中的 exe 被安全软件占用导致改名失败，只留下一个 `.new` 残骸。同步改写 6.6、6.7、6.8、6.9、阶段 5、测试清单与风险表。
