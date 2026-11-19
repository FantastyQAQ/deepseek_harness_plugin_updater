#!/usr/bin/env bash
# 【仅适用于裸装 Nginx 的服务器】在服务器上新增在线更新站点
#
# ⚠️ 如果你的服务器装了宝塔面板（/www/server/panel 存在），不要运行本脚本！
#    宝塔的 Nginx 不在 PATH、站点配置在 /www/server/panel/vhost/nginx/，
#    本脚本会失败或干扰现有站点。宝塔环境请按 docs/server_setup_guide.md
#    在面板界面配置（HTML 项目 + 改 listen 8080）。
#
# 用法：
#   sudo bash setup_server.sh <端口>
# 例：
#   sudo bash setup_server.sh 8080
#
# 设计原则（服务器上已有别的站点时尤其重要）：
#  1. 不安装、不升级、不卸载 Nginx —— 你没装就报错退出，交由你决定怎么装；
#  2. 只在 conf.d/ 下新增一个独立文件，不碰 nginx.conf 与其它站点配置；
#  3. 不使用 default_server，避免与已有站点抢同端口的默认虚拟主机；
#  4. 不直接 restart，而是 nginx -t 通过后 reload（不断连接）；
#  5. 任一步失败自动删除新增文件并回滚，现有服务保持原样；
#  6. 只对新增的 server 块监听端口放行防火墙，不动其它规则。

set -euo pipefail

PORT="${1:-}"
APP_ID="dsh_plugin_updater"
ROOT_DIR="/srv/qttools_updates"
CONF_NAME="qttools_updates.conf"
TARGET_CONF="/etc/nginx/conf.d/${CONF_NAME}"

die() { echo "错误：$*" >&2; exit 1; }

if [ "$(id -u)" -ne 0 ]; then
    die "请用 root 或 sudo 执行"
fi

# 宝塔环境直接拒绝执行，避免破坏现有站点
if [ -d /www/server/panel ]; then
    die "检测到宝塔面板，本脚本不适用。
     请改用面板界面：网站 → HTML项目 → 添加项目，
     再把 listen 改 8080 并加缓存规则。
     详见 docs/server_setup_guide.md。"
fi
if [ -z "$PORT" ]; then
    die "用法：sudo bash setup_server.sh <端口>，例如：sudo bash setup_server.sh 8080"
fi
if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    die "端口必须是 1-65535 的数字，当前：$PORT"
fi

# ---------- 0. 前置检查：Nginx 必须先装好 ----------
# 已有服务器上贸然安装/升级 Nginx 有覆盖配置的风险，这里只检查不安装
command -v nginx >/dev/null 2>&1 || die "未检测到 nginx。本脚本不负责安装，请先自行安装后再执行。"
echo "[0/6] nginx 已存在：$(nginx -v 2>&1)"

# ---------- 1. 端口冲突检查 ----------
# 若该端口已被别的进程监听（含其它 nginx 站点），直接停下让人工决定
if command -v ss >/dev/null 2>&1; then
    if ss -lntp 2>/dev/null | awk -v p=":${PORT}" '$4 ~ p"$"' | grep -q .; then
        echo ""
        echo "以下进程正在监听 ${PORT}："
        ss -lntp 2>/dev/null | awk -v p=":${PORT}" '$4 ~ p"$"' | sed 's/^/  /'
        echo ""
        die "端口 ${PORT} 已被占用。请换一个空闲端口重新执行（例如 8081）。"
    fi
fi
echo "[1/6] 端口 ${PORT} 空闲"

# ---------- 2. 备份旧配置（若上次已经部署过）----------
if [ -f "$TARGET_CONF" ]; then
    BACKUP="${TARGET_CONF}.bak-$(date +%Y%m%d-%H%M%S)"
    cp -a "$TARGET_CONF" "$BACKUP"
    echo "[2/6] 已备份旧配置到 $BACKUP"
else
    echo "[2/6] 首次部署，无需备份"
fi

# ---------- 3. 目录结构 ----------
# 上传用户可写，Nginx 进程可读（a+rX 让目录可进入、文件可读）
mkdir -p "${ROOT_DIR}/${APP_ID}/releases"
chown -R "${SUDO_USER:-root}" "$ROOT_DIR"
chmod -R a+rX "$ROOT_DIR"
echo "[3/6] 目录就绪：${ROOT_DIR}/${APP_ID}/"

# ---------- 4. 写入站点配置（独立文件，不碰别人）----------
# 注意：listen 不带 default_server。
#   default_server 会抢占该端口的默认虚拟主机；若同端口已有站点，
#   nginx -t 会直接报 "a duplicate default server"，导致 reload 失败。
#   这里用 server_name _ 兜底：该端口若没有别的 server 块，它自然成为唯一入口。
cat > "$TARGET_CONF" <<EOF
# 由 dsh_plugin_updater 的 setup_server.sh 生成，请勿手工改动其它站点配置。
server {
    listen ${PORT};
    server_name _;

    root ${ROOT_DIR};
    autoindex off;

    # 清单每次都要取最新，禁止缓存（匹配任意 APP_ID 子目录）
    location ~ ^/[a-z0-9_]+/latest\.json(\.sig)?\$ {
        add_header Cache-Control "no-cache";
    }

    # 版本目录一经发布不再修改，可以长期缓存
    location ~ ^/[a-z0-9_]+/releases/ {
        add_header Cache-Control "public, max-age=31536000";
    }

    # 其它路径一律 404，避免把目录当静态网站暴露
    location / {
        return 404;
    }
}
EOF
echo "[4/6] 已写入 $TARGET_CONF"

# ---------- 5. 校验并平滑生效 ----------
# nginx -t 会解析全部配置：任何一个站点写错都会失败。
# 失败时删掉本次新增的文件，保证现有服务不被牵连。
if ! nginx -t 2>&1 | sed 's/^/  /'; then
    echo ""
    echo "nginx -t 未通过，正在删除本次新增的配置以恢复原状…" >&2
    rm -f "$TARGET_CONF"
    if [ -n "${BACKUP:-}" ] && [ -f "$BACKUP" ]; then
        mv -f "$BACKUP" "$TARGET_CONF"
        echo "已恢复备份：$TARGET_CONF" >&2
    fi
    die "配置校验失败，未做任何改动（现有站点不受影响）"
fi

# reload 而非 restart：不中断已有连接，已有站点完全无感
if ! systemctl reload nginx 2>/dev/null; then
    # 少数环境没有 systemctl reload，退回 nginx -s reload
    nginx -s reload 2>/dev/null || die "reload 失败，请检查 systemctl status nginx"
fi
echo "[5/6] 配置已平滑生效（reload，服务未中断）"

# 校验配置确实被加载：若 nginx.conf 没有 include conf.d/*.conf，
# 文件写进去了也不会生效，这里主动发现而不是等手机实测才发现 404
if ! nginx -T 2>/dev/null | grep -qE "listen\s+${PORT}([^0-9]|$)"; then
    echo ""
    echo "警告：未能从 nginx -T 中确认 ${PORT} 已被加载。" >&2
    echo "      可能 /etc/nginx/nginx.conf 没有 include conf.d/*.conf。" >&2
    echo "      请检查 nginx.conf 的 include 行，或改用 sites-enabled 目录。" >&2
fi

# ---------- 6. 系统防火墙（只放行本端口）----------
if command -v firewall-cmd >/dev/null 2>&1 && firewall-cmd --state >/dev/null 2>&1; then
    firewall-cmd --permanent --add-port="${PORT}/tcp" >/dev/null
    firewall-cmd --reload >/dev/null
    echo "[6/6] firewalld 已放行 ${PORT}/tcp"
elif command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q "Status: active"; then
    ufw allow "${PORT}/tcp" >/dev/null
    echo "[6/6] ufw 已放行 ${PORT}/tcp"
else
    echo "[6/6] 未检测到启用的系统防火墙，跳过（如已启用请自行放行 ${PORT}/tcp）"
fi

# ---------- 完成提示 ----------
echo ""
echo "=============================================="
echo " 完成。还需三步人工确认："
echo "=============================================="
echo " 1. 阿里云控制台 → 安全组 → 入方向放行 TCP ${PORT}"
echo "    （这一步脚本管不到，必须手工配置）"
echo ""
echo " 2. 上传清单与签名："
echo "    scp publish_info/latest.json publish_info/latest.json.sig root@<IP>:${ROOT_DIR}/${APP_ID}/"
echo ""
echo " 3. 用手机流量（关 Wi-Fi）访问验证："
echo "    http://<公网IP>:${PORT}/${APP_ID}/latest.json"
echo ""
echo " 现有站点未受任何影响：本脚本只新增了 ${TARGET_CONF}，"
echo " 采用的是 reload 而非 restart，已有连接不会被断开。"
echo ""
echo " 如需卸载本站点："
echo "    sudo rm ${TARGET_CONF} && sudo nginx -t && sudo systemctl reload nginx"
echo "=============================================="
