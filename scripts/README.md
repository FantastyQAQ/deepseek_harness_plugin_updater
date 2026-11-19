# scripts 目录说明

本目录存放发布、服务器部署与版本查询相关的辅助脚本。除 `npm_dist_tags.js` 会在程序
运行时被调用外，其余脚本都在工程外手工执行。

| 文件 | 作用 | 调用方式 | 运行环境 |
|---|---|---|---|
| `publish.ps1` | 对打包后的 exe 生成并签名发布清单 `latest.json` | 手工执行（发版时本机运行） | Windows PowerShell（本机） |
| `setup_server.sh` | 在**裸装 Nginx** 的服务器上新增在线更新站点 | 手工执行（服务器首次部署） | Linux + Nginx（root） |
| `check_server.sh` | 只读体检服务器现状，动 Nginx 前先看清环境 | 手工执行（部署/排障前） | Linux（root，只查不改） |
| `npm_dist_tags.js` | 批量查询 npm 包最新版本与发布时间 | **程序内部调用**（内嵌于 qrc，运行时落盘执行），也可手工跑 | Node.js |

---

## publish.ps1

生成在线更新用的 `latest.json`（含版本、大小、SHA256、下载地址、更新说明），
并用 `update_keys/` 下的私钥做 PKCS#1 v1.5 + SHA-256 签名，同时生成 `latest.json.sig`。

**前置条件**

- 已用 EVB 等工具打包出待发布的 exe（不是 `release/` 下的裸 exe）；
- `update_keys/update_sign_private.pem` 存在（该目录被 `.gitignore` 排除，勿外传）；
- `openssl` 在 `PATH` 中。

**用法**（在工程根目录执行）

```powershell
powershell -ExecutionPolicy Bypass -File scripts/publish.ps1 `
    -Exe pack\releases\SV01.011\dsh_plugin_updater_SV01.011.exe `
    -Version SV01.011 `
    -Notes "新增在线更新功能；修复若干界面问题"
```

**参数**

| 参数 | 必填 | 说明 |
|---|---|---|
| `-Exe` | 是 | 待发布的 exe 路径，其文件名会原样写进清单 `url`，上传时无需改名 |
| `-Version` | 是 | 版本号，必须与 `user/inc/app_info.h` 的 `APP_VERSION` 一致，否则直接报错 |
| `-Notes` | 否 | 更新说明，可传字面量 `\n` 表示换行 |
| `-PrivateKey` | 否 | 签名私钥，默认 `update_keys/update_sign_private.pem` |
| `-AppId` | 否 | 应用 ID，默认 `dsh_plugin_updater`，决定清单 `url` 前缀与上传子目录 |
| `-OutDir` | 否 | 产物目录，默认 `publish_info`（该目录被 `.gitignore` 排除） |

**产物**：`publish_info/latest.json`、`publish_info/latest.json.sig`（两个文件都要上传）。

**注意**

- 脚本会先用配对公钥自检签名，自检失败即中止，避免把坏签名传上服务器；
- 清单签名后**任何改动都会导致客户端验签失败**，要改内容只能重新执行本脚本；
- 脚本末尾会打印 `scp` 上传命令（先传 exe、再传清单与签名，最后 `chown -R www:www`）。

详细部署流程见 `docs/server_setup_guide.md`。

---

## setup_server.sh

在服务器上把 `/srv/qttools_updates` 目录与对应的 Nginx 站点配置一次性建好。

**⚠️ 仅适用于裸装 Nginx。** 服务器存在 `/www/server/panel`（宝塔面板）时脚本会直接
报错退出——宝塔的 Nginx 不在 `PATH`、站点配置在面板目录下，请按
`docs/server_setup_guide.md` 用面板界面配置。

**用法**

```bash
sudo bash setup_server.sh 8080
```

参数为站点监听端口（1–65535，需先确认为空闲端口）。

**脚本做了什么**

1. 检查 `nginx` 已安装（**不安装、不升级、不卸载**）、端口未被占用；
2. 已有同名配置先按时间戳备份到 `qttools_updates.conf.bak-<时间>`；
3. 创建 `/srv/qttools_updates/<APP_ID>/releases` 目录并放开只读权限；
4. 只在 `/etc/nginx/conf.d/` 新增一个独立配置文件，不碰 `nginx.conf` 与其它站点；
   - 不使用 `default_server`，避免抢同端口默认虚拟主机；
   - `latest.json(.sig)` 不缓存，`releases/` 长期缓存，其它路径一律 404；
5. `nginx -t` 通过后 `reload`（不用 `restart`，不断已有连接）；失败自动删除本次
   新增文件并回滚备份；
6. 只对本端口放行 `firewalld` / `ufw`。

**完成后仍需人工做**：阿里云安全组放行该 TCP 端口、上传清单与签名、用手机流量访问
`http://<公网IP>:<端口>/dsh_plugin_updater/latest.json` 验证。

**卸载站点**

```bash
sudo rm /etc/nginx/conf.d/qttools_updates.conf && sudo nginx -t && sudo systemctl reload nginx
```

---

## check_server.sh

部署或排障前先跑一遍，**只读**：只做 `cat` / `grep` / `ls` / `ss` / `nginx -T` 等查询，
不写任何文件、不改任何配置，同时兼容裸装 Nginx 与宝塔面板两种环境。

**用法**

```bash
sudo bash check_server.sh 8080
```

端口参数可省略，默认 `8080`。

**输出内容（8 节）**

1. 系统版本、是否宝塔、Nginx 路径与工作进程用户；
2. 配置布局：`conf.d/`、`sites-enabled/` 或宝塔 `vhost/nginx/` 的 include 情况；
3. 目标端口是否被占用；
4. 监听该端口的 server 块片段（宝塔环境会提示是否误配了 `proxy_pass`）；
5. Nginx `is-active` / `is-enabled`；
6. `firewalld` / `ufw` / 宝塔面板三层防火墙的放行情况；
7. `qttools_updates` 更新目录是否存在及其权限；
8. 本机 `curl` 直连测试，并给出 502 / 403 / 404 / 000 的含义提示。

---

## npm_dist_tags.js

批量查询 npm 包的最新版本与发布时间。**主要供程序内部调用**：该文件被编进
`resources.qrc`（资源路径 `:/scripts/npm_dist_tags.js`），程序启动查询时会把它落盘到
临时目录再用单个 node 进程执行——不再为每个包单独起 `npm.cmd`，并受 DSH
`minimumReleaseAge = 24h` 策略影响：最新版发布不足 24 小时时，额外给出一个满 24 小时
的可用稳定版。

**用法**

```bash
node npm_dist_tags.js                 # 从标准输入逐行读取包名
node npm_dist_tags.js pkg1 pkg2       # 或直接用命令行参数传入
```

**输出**（逐行流式打印，供宿主实时解析）

```
<包名>\t<最新版本>\t<发布时间ISO8601>\t<可用最新版本>   查询成功
<包名>\t!<错误信息>                                    查询失败
```

可用最新版本为空表示没有满 24 小时的稳定版；发布时间为空表示 registry 未提供 `time`。

**退出码**：恒为 0——个别包失败只影响对应行，不被当作整体失败。

**依赖与配置**：需 Node.js（使用内置 `https` / `zlib`，无第三方依赖）；会自动读取
`~/.npmrc` 的 `registry`、`@scope:registry` 与 `//host/:_authToken`，因此支持私有源与
鉴权。并发上限 16，单请求超时 20 秒。
