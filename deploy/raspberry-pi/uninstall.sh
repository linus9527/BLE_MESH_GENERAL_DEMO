#!/usr/bin/env bash
set -Eeuo pipefail

INSTALL_ROOT="/opt/ble-mesh-gateway"
DATA_DIR="/var/lib/ble-mesh-gateway"
CONFIG_DIR="/etc/ble-mesh-gateway"
SERVICE_NAME="ble-mesh-gateway.service"
APP_USER="blemesh"
PURGE=0
ASSUME_YES=0

usage() {
    cat <<'EOF'
用法：sudo bash ./uninstall.sh [--purge] [--yes]

默认只移除程序和 systemd 服务，保留配置、历史数据库和备份。
--purge 同时删除 /var/lib/ble-mesh-gateway 和 /etc/ble-mesh-gateway。
EOF
}

for argument in "$@"; do
    case "$argument" in
        --purge) PURGE=1 ;;
        --yes) ASSUME_YES=1 ;;
        --help|-h) usage; exit 0 ;;
        *) printf '未知参数：%s\n' "$argument" >&2; exit 1 ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    printf '请使用 sudo 执行。\n' >&2
    exit 1
fi

if [[ $ASSUME_YES != 1 ]]; then
    printf '将移除 BLE Mesh 网关程序。继续？[y/N] '
    read -r answer
    [[ "$answer" == [yY] ]] || exit 0
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl disable --now "$SERVICE_NAME" >/dev/null 2>&1 || true
    rm -f "/etc/systemd/system/$SERVICE_NAME"
    systemctl daemon-reload || true
fi

case "$INSTALL_ROOT" in /opt/ble-mesh-gateway) rm -rf "$INSTALL_ROOT" ;; *) printf '安装目录校验失败。\n' >&2; exit 1 ;; esac
if [[ $PURGE == 1 ]]; then
    case "$DATA_DIR" in /var/lib/ble-mesh-gateway) rm -rf "$DATA_DIR" ;; *) printf '数据目录校验失败。\n' >&2; exit 1 ;; esac
    case "$CONFIG_DIR" in /etc/ble-mesh-gateway) rm -rf "$CONFIG_DIR" ;; *) printf '配置目录校验失败。\n' >&2; exit 1 ;; esac
fi

if id -u "$APP_USER" >/dev/null 2>&1; then
    userdel "$APP_USER" >/dev/null 2>&1 || true
fi
printf 'BLE Mesh 网关卸载完成。\n'
