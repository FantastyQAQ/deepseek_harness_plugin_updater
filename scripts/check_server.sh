#!/usr/bin/env bash
# 服务器现状体检（只读，绝不修改任何东西）
#
# 用法：
#   sudo bash check_server.sh 8080
#
# 目的：动 Nginx 之前先看清现状，避免瞎改跑着的服务。
# 本脚本只做查询：cat / grep / ls / ss / nginx -T 等，不写任何文件。
# 同时兼容「裸装 Nginx」与「宝塔面板」两种环境。

set -uo pipefail

PORT="${1:-8080}"

# ---------- 环境探测 ----------
# 宝塔的 nginx 不在 PATH 中，这里先定位可执行文件
NGINX_BIN=""
if command -v nginx >/dev/null 2>&1; then
    NGINX_BIN="$(command -v nginx)"
elif [ -x /www/server/nginx/sbin/nginx ]; then
    NGINX_BIN="/www/server/nginx/sbin/nginx"
fi

BT_PANEL=0
[ -d /www/server/panel ] && BT_PANEL=1

echo "=============================================="
echo " 服务器现状体检（只读）  端口 ${PORT}"
echo "=============================================="
echo ""

# ---------- 1. 系统与面板 ----------
echo "--- [1] 系统 / 面板 / Nginx ---"
[ -f /etc/os-release ] && grep -E '^(PRETTY_NAME)=' /etc/os-release | sed 's/"//g'
if [ "$BT_PANEL" -eq 1 ]; then
    echo "面板：宝塔（/www/server/panel 存在）"
else
    echo "面板：未检测到宝塔"
fi
if [ -n "$NGINX_BIN" ]; then
    echo "nginx：$NGINX_BIN  ($("$NGINX_BIN" -v 2>&1))"
    # Nginx 运行用户（权限问题的关键）
    ps -eo user,comm 2>/dev/null | awk '$2=="nginx"{print "  nginx 工作进程用户: "$1; exit}'
else
    echo "nginx：未找到（PATH 与宝塔路径均不存在）"
fi
echo ""

# ---------- 2. 配置布局 ----------
echo "--- [2] 配置布局 ---"
if [ "$BT_PANEL" -eq 1 ]; then
    echo "宝塔站点配置目录：/www/server/panel/vhost/nginx/"
    ls -1 /www/server/panel/vhost/nginx/ 2>/dev/null | sed 's/^/  /' || echo "  （空或不可读）"
else
    echo "/etc/nginx/ 下的 include 行："
    grep -nE '^\s*include' /etc/nginx/nginx.conf 2>/dev/null | sed 's/^/  /' || echo "  （读不到 nginx.conf）"
    echo "conf.d/："
    ls -1 /etc/nginx/conf.d/ 2>/dev/null | sed 's/^/  /' || echo "  （无此目录）"
    echo "sites-enabled/："
    ls -1 /etc/nginx/sites-enabled/ 2>/dev/null | sed 's/^/  /' || echo "  （无此目录）"
fi
echo ""

# ---------- 3. 端口占用 ----------
echo "--- [3] 端口 ${PORT} 占用情况 ---"
if command -v ss >/dev/null 2>&1; then
    OCCUPIED=$(ss -lntp 2>/dev/null | awk -v p=":${PORT}" '$4 ~ p"$"')
    if [ -n "$OCCUPIED" ]; then
        echo "$OCCUPIED" | sed 's/^/  /'
        echo "  >>> 已被占用。若是 nginx 说明该端口有站点，请换端口或并入现有站点。"
    else
        echo "  空闲"
    fi
else
    echo "  （无 ss 命令）"
fi
echo ""

# ---------- 4. 该端口对应的 server 块 ----------
echo "--- [4] 监听 ${PORT} 的配置片段 ---"
if [ -n "$NGINX_BIN" ]; then
    if [ "$BT_PANEL" -eq 1 ]; then
        grep -rn -A 15 "listen ${PORT}" /www/server/panel/vhost/nginx/ 2>/dev/null | sed 's/^/  /' \
            || echo "  未找到监听 ${PORT} 的站点"
        echo ""
        echo "  >>> 重点：上面若出现 proxy_pass，说明站点是「反向代理」，"
        echo "      8080 会返回 502。需要重建为 HTML 项目。"
    else
        "$NGINX_BIN" -T 2>/dev/null | grep -nE "^\s*(listen|server_name|root|proxy_pass)\s" | sed 's/^/  /'
    fi
else
    echo "  （nginx 不可用，跳过）"
fi
echo ""

# ---------- 5. 运行状态 ----------
echo "--- [5] Nginx 运行状态 ---"
if command -v systemctl >/dev/null 2>&1; then
    echo "  is-active : $(systemctl is-active nginx 2>/dev/null)"
    echo "  is-enabled: $(systemctl is-enabled nginx 2>/dev/null)"
fi
echo ""

# ---------- 6. 防火墙 ----------
echo "--- [6] 防火墙 ---"
if command -v firewall-cmd >/dev/null 2>&1 && firewall-cmd --state >/dev/null 2>&1; then
    echo "  firewalld 运行中，已放行端口："
    firewall-cmd --list-ports 2>/dev/null | tr ' ' '\n' | sed 's/^/    /'
elif command -v ufw >/dev/null 2>&1; then
    echo "  ufw: $(ufw status 2>/dev/null | head -1)"
else
    echo "  未检测到 firewalld / ufw"
fi
# 宝塔自带防火墙（面板「安全」页），与系统防火墙是两层
if [ "$BT_PANEL" -eq 1 ]; then
    echo "  提醒：宝塔面板「安全」页是独立的一层，需单独放行 ${PORT}"
fi
echo ""

# ---------- 7. 更新目录 ----------
echo "--- [7] 更新目录 ---"
for d in /www/wwwroot/qttools_updates /srv/qttools_updates; do
    if [ -d "$d" ]; then
        echo "$d 存在（$(stat -c '%U:%G %a' "$d" 2>/dev/null)）："
        ls -1R "$d" 2>/dev/null | head -20 | sed 's/^/  /'
    else
        echo "$d 不存在"
    fi
done
echo ""

# ---------- 8. 本地直连测试 ----------
echo "--- [8] 本地直连测试 ---"
if command -v curl >/dev/null 2>&1; then
    for path in "/" "/dsh_plugin_updater/latest.json"; do
        code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 "http://127.0.0.1:${PORT}${path}" 2>/dev/null)
        echo "  ${path} => ${code:-无响应}"
    done
    echo "  （502=反向代理配错；403=权限；404=文件缺失；000=未监听）"
else
    echo "  （无 curl）"
fi
echo ""

echo "=============================================="
echo " 体检结束。把输出贴回来，据此定位问题。"
echo "=============================================="
