#!/usr/bin/env bash
set -Eeuo pipefail
IFS=$'\n\t'

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_ROOT="/opt/ble-mesh-gateway"
APP_DIR="$INSTALL_ROOT/app"
RUNTIME_DIR="$INSTALL_ROOT/runtime"
FIRMWARE_DIR="$INSTALL_ROOT/firmware"
DATA_DIR="/var/lib/ble-mesh-gateway"
CONFIG_DIR="/etc/ble-mesh-gateway"
ENV_FILE="$CONFIG_DIR/ble-mesh-gateway.env"
SERVICE_NAME="ble-mesh-gateway.service"
APP_USER="blemesh"
PACKAGE_VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/VERSION" 2>/dev/null || printf '%s' unknown)"
MODE=install
START_SERVICE=1
TEMP_DIR=""

log() { printf '[ble-mesh] %s\n' "$*"; }
warn() { printf '[ble-mesh] 警告：%s\n' "$*" >&2; }
die() { printf '[ble-mesh] 错误：%s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
用法：sudo bash ./install.sh [--upgrade] [--no-start]

首次安装会创建 blemesh 服务用户、安装 ARM Node.js 和 Node-RED 依赖，并注册 systemd 服务。
升级模式会先备份现场配置，再更新发布包中的运行代码。
EOF
}

for argument in "$@"; do
    case "$argument" in
        --upgrade) MODE=upgrade ;;
        --no-start) START_SERVICE=0 ;;
        --help|-h) usage; exit 0 ;;
        *) die "未知参数：$argument" ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    die "请使用 sudo 执行。"
fi

if [[ -d "$APP_DIR" && $MODE == install ]]; then
    MODE=upgrade
fi

if [[ -f "$ENV_FILE" ]]; then
    set -a
    . "$ENV_FILE"
    set +a
fi

NODE_VERSION="${NODE_VERSION:-24.19.0}"
PNPM_VERSION="${PNPM_VERSION:-9.15.9}"
ALLOW_ARMV7L="${ALLOW_ARMV7L:-0}"
INSTALL_BUILD_TOOLS="${INSTALL_BUILD_TOOLS:-1}"
NODE_RUNTIME_ARCHIVE="${NODE_RUNTIME_ARCHIVE:-}"
NODE_RUNTIME_SHA256="${NODE_RUNTIME_SHA256:-}"

cleanup() {
    if [[ -n "$TEMP_DIR" && -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT

command_exists() { command -v "$1" >/dev/null 2>&1; }

install_apt_packages() {
    local packages=("$@")
    if ! command_exists apt-get; then
        die "缺少命令：${packages[*]}；当前系统没有 apt-get，请手工安装后重试。"
    fi
    log "安装系统依赖：${packages[*]}"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y "${packages[@]}"
}

ensure_base_tools() {
    local missing=()
    for command_name in curl tar xz sha256sum; do
        if ! command_exists "$command_name"; then
            missing+=("$command_name")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        install_apt_packages ca-certificates curl tar xz-utils coreutils
    fi
    if [[ "$INSTALL_BUILD_TOOLS" == 1 ]]; then
        local build_missing=()
        for command_name in gcc g++ make python3; do
            if ! command_exists "$command_name"; then
                build_missing+=("$command_name")
            fi
        done
        if [[ ${#build_missing[@]} -gt 0 ]]; then
            install_apt_packages build-essential python3
        fi
    fi
}

detect_node_arch() {
    case "$(uname -m)" in
        aarch64|arm64) NODE_ARCH=arm64 ;;
        armv7l|armv7*)
            if [[ "$ALLOW_ARMV7L" != 1 ]]; then
                die "检测到 32 位 ARM（$(uname -m)）。Node-RED 5.x 发布包按 64 位 Raspberry Pi OS 测试；请改用 64 位系统，或明确设置 ALLOW_ARMV7L=1 后自行验证。"
            fi
            NODE_ARCH=armv7l
            warn "使用 armv7l 兼容模式，Node-RED 5.x 可能存在第三方节点兼容性问题。"
            ;;
        *) die "不支持的 CPU 架构：$(uname -m)，本包只支持 aarch64 或 armv7l。" ;;
    esac
}

service_exists() {
    command_exists systemctl && systemctl cat "$SERVICE_NAME" >/dev/null 2>&1
}

stop_service() {
    if service_exists; then
        systemctl stop "$SERVICE_NAME" || true
    fi
}

create_user_and_dirs() {
    if ! id -u "$APP_USER" >/dev/null 2>&1; then
        useradd --system --create-home --home-dir "$DATA_DIR" --shell /usr/sbin/nologin "$APP_USER"
    fi
    if getent group dialout >/dev/null 2>&1; then
        usermod -a -G dialout "$APP_USER"
    else
        warn "系统没有 dialout 组，串口权限需要手工配置。"
    fi
    install -d -m 0755 "$INSTALL_ROOT" "$APP_DIR" "$FIRMWARE_DIR" "$DATA_DIR" "$DATA_DIR/data" "$DATA_DIR/backups" "$CONFIG_DIR"
}

backup_mutable_files() {
    local backup_dir="$DATA_DIR/backups/$(date +%Y%m%d-%H%M%S)"
    install -d -m 0750 "$backup_dir"
    for relative_path in flows.json flows_cred.json settings.js package.json pnpm-lock.yaml; do
        if [[ -e "$APP_DIR/$relative_path" ]]; then
            cp -a "$APP_DIR/$relative_path" "$backup_dir/"
        fi
    done
    if [[ -f "$ENV_FILE" ]]; then
        cp -a "$ENV_FILE" "$backup_dir/"
    fi
    log "现场配置已备份到 $backup_dir"
}

copy_app() {
    if [[ $MODE == install && ! -e "$APP_DIR/package.json" ]]; then
        cp -a "$SCRIPT_DIR/app/." "$APP_DIR/"
        return
    fi
    backup_mutable_files
    for relative_path in lib scripts pnpm-workspace.yaml README.md Dockerfile docker-compose.yml .env.example; do
        if [[ -e "$SCRIPT_DIR/app/$relative_path" ]]; then
            rm -rf "$APP_DIR/$relative_path"
            cp -a "$SCRIPT_DIR/app/$relative_path" "$APP_DIR/$relative_path"
        fi
    done
    for relative_path in flows.json settings.js package.json pnpm-lock.yaml; do
        if [[ ! -e "$APP_DIR/$relative_path" && -e "$SCRIPT_DIR/app/$relative_path" ]]; then
            cp -a "$SCRIPT_DIR/app/$relative_path" "$APP_DIR/$relative_path"
        fi
    done
}

copy_launcher() {
    [[ -f "$SCRIPT_DIR/main" ]] || die "发行包缺少 Linux 启动入口：$SCRIPT_DIR/main"
    install -m 0755 "$SCRIPT_DIR/main" "$INSTALL_ROOT/main"
}

copy_firmware() {
    rm -rf "$FIRMWARE_DIR"
    install -d -m 0755 "$FIRMWARE_DIR"
    if [[ -d "$SCRIPT_DIR/firmware" ]]; then
        cp -a "$SCRIPT_DIR/firmware/." "$FIRMWARE_DIR/"
    fi
}

create_config() {
    if [[ ! -f "$ENV_FILE" ]]; then
        install -m 0640 "$SCRIPT_DIR/config/ble-mesh-gateway.env.example" "$ENV_FILE"
        local secret=""
        if command_exists openssl; then
            secret="$(openssl rand -hex 32)"
        else
            secret="$(od -An -N32 -tx1 /dev/urandom | tr -d '[:space:]')"
        fi
        sed -i "s/^NODE_RED_CREDENTIAL_SECRET=.*/NODE_RED_CREDENTIAL_SECRET=$secret/" "$ENV_FILE"
    fi
    chown root:"$APP_USER" "$ENV_FILE"
    chmod 0640 "$ENV_FILE"
}

install_runtime() {
    detect_node_arch
    local current_version=""
    if [[ -x "$RUNTIME_DIR/bin/node" ]]; then
        current_version="$("$RUNTIME_DIR/bin/node" --version 2>/dev/null | tr -d 'v' || true)"
    fi
    if [[ "$current_version" == "$NODE_VERSION" && -x "$RUNTIME_DIR/bin/pnpm" ]]; then
        log "复用 Node.js $current_version 运行时。"
        return
    fi

    TEMP_DIR="$(mktemp -d /tmp/ble-mesh-gateway.XXXXXX)"
    local archive="$TEMP_DIR/node-v$NODE_VERSION-linux-$NODE_ARCH.tar.xz"
    local tarball_name="node-v$NODE_VERSION-linux-$NODE_ARCH.tar.xz"
    local download_url="https://nodejs.org/dist/v$NODE_VERSION/$tarball_name"

    if [[ -n "$NODE_RUNTIME_ARCHIVE" ]]; then
        [[ -f "$NODE_RUNTIME_ARCHIVE" ]] || die "NODE_RUNTIME_ARCHIVE 不存在：$NODE_RUNTIME_ARCHIVE"
        cp -f "$NODE_RUNTIME_ARCHIVE" "$archive"
    else
        log "下载 Node.js $NODE_VERSION ($NODE_ARCH)"
        curl --fail --location --retry 3 --retry-delay 2 --output "$archive" "$download_url"
    fi

    local expected_sha="$NODE_RUNTIME_SHA256"
    if [[ -z "$expected_sha" ]]; then
        expected_sha="$(curl --fail --location --retry 3 "https://nodejs.org/dist/v$NODE_VERSION/SHASUMS256.txt" | awk -v name="$tarball_name" '$2 == name { print $1; exit }')"
    fi
    [[ -n "$expected_sha" ]] || die "无法取得 Node.js 校验值：$tarball_name"
    printf '%s  %s\n' "$expected_sha" "$archive" | sha256sum -c -

    tar -xJf "$archive" -C "$TEMP_DIR"
    local extracted="$TEMP_DIR/node-v$NODE_VERSION-linux-$NODE_ARCH"
    [[ -x "$extracted/bin/node" ]] || die "Node.js 压缩包结构不符合预期。"
    rm -rf "$RUNTIME_DIR"
    mv "$extracted" "$RUNTIME_DIR"
    "$RUNTIME_DIR/bin/npm" install --global --no-audit --no-fund --prefix "$RUNTIME_DIR" "pnpm@$PNPM_VERSION"
    [[ -x "$RUNTIME_DIR/bin/node" && -x "$RUNTIME_DIR/bin/pnpm" ]] || die "Node.js 或 pnpm 安装不完整。"
}

install_node_dependencies() {
    local pnpm_bin="$RUNTIME_DIR/bin/pnpm"
    [[ -x "$pnpm_bin" ]] || die "未找到 pnpm：$pnpm_bin"
    [[ -f "$APP_DIR/package.json" && -f "$APP_DIR/pnpm-lock.yaml" ]] || die "Node-RED 工程缺少 package.json 或 pnpm-lock.yaml。"
    log "安装 Node-RED 生产依赖。"
    export PATH="$RUNTIME_DIR/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    export HOME="$DATA_DIR"
    if ! "$pnpm_bin" --dir "$APP_DIR" install --prod --frozen-lockfile; then
        warn "现场 package.json 与 pnpm-lock.yaml 不完全同步，改用普通生产安装以保留自定义节点。"
        "$pnpm_bin" --dir "$APP_DIR" install --prod
    fi
    "$pnpm_bin" --dir "$APP_DIR" rebuild
}

install_service() {
    install -m 0644 "$SCRIPT_DIR/systemd/ble-mesh-gateway.service" "/etc/systemd/system/$SERVICE_NAME"
    if command_exists systemctl; then
        systemctl daemon-reload
        systemctl enable "$SERVICE_NAME" >/dev/null
        if [[ $START_SERVICE == 1 ]]; then
            systemctl restart "$SERVICE_NAME"
        fi
    else
        warn "当前系统没有 systemctl，已完成文件安装但未注册开机服务。"
    fi
}

show_summary() {
    local host_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
    host_ip="${host_ip:-树莓派IP}"
    log "版本：$PACKAGE_VERSION"
    log "Node-RED Dashboard：http://$host_ip:${PORT:-1880}/dashboard/sensors"
    log "Node-RED 编辑器：http://$host_ip:${PORT:-1880}"
    log "配置文件：$ENV_FILE"
    log "服务日志：sudo journalctl -u $SERVICE_NAME -f"
    log "网关固件：$FIRMWARE_DIR/gateway-merged.hex 和 $FIRMWARE_DIR/gateway.uf2"
}

log "开始${MODE}模式安装 BLE Mesh 网关 $PACKAGE_VERSION。"
ensure_base_tools
stop_service
create_user_and_dirs
copy_app
copy_launcher
copy_firmware
create_config
install_runtime
install_node_dependencies
chown -R "$APP_USER:$APP_USER" "$APP_DIR" "$DATA_DIR" "$FIRMWARE_DIR"
install_service
show_summary
