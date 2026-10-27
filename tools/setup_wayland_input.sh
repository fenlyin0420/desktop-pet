#!/usr/bin/env bash
# Wayland 会话下让 fxpet 能读全局键鼠（evdev）。
#
# Wayland 没有“全局键鼠监听”的协议，XWayland 也只转发自己窗口上的事件，
# 所以 fxpet 直接读 /dev/input/event*。多数发行版默认只有 root 和 input 组可读
# （也有会话已经由 logind 发过 ACL），这里提供两种授权方式（外加一个只读检查）：
#
#   tools/setup_wayland_input.sh uaccess   # 推荐：装一条 udev uaccess 规则
#   tools/setup_wayland_input.sh group     # 把当前用户加入 input 组（重新登录生效）
#   tools/setup_wayland_input.sh check     # 默认动作：只检查现在能不能读，不改任何东西
#
# 两者的区别（都会让“以该用户身份运行的所有程序”可以读键鼠，不只是 fxpet）：
#   uaccess：ACL 只发给“当前活动的本地会话用户”，切用户/注销即失效；
#            撤销 = 删除 /etc/udev/rules.d/70-fxpet-input.rules 后
#            `sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=input`
#   group  ：把账号永久加入 input 组；撤销 = `sudo gpasswd -d <用户> input`
#
# 注意：uaccess 的 ACL 由 systemd-logind 发放，没有 logind 的系统（runit/s6/openrc）
# 里这条规则不会生效，那种环境请改用 group 子命令。
set -euo pipefail

RULE_PATH=/etc/udev/rules.d/70-fxpet-input.rules
RULE_CONTENT='# fxpet: 允许当前活动会话用户读取输入设备（Wayland 下监听全局键鼠）
KERNEL=="event*", SUBSYSTEM=="input", TAG+="uaccess"'

usage() {
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
}

# 以 root 直接执行时不必（也可能没有）sudo
run_root() {
    if [ "${EUID:-$(id -u)}" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "需要 root 权限执行：$*（当前环境没有 sudo，请用 root 运行本脚本）" >&2
        exit 1
    fi
}

check() {
    local dir=${FXPET_EVDEV_DIR:-/dev/input}
    local readable=0 total=0 device name
    for device in "$dir"/event*; do
        [ -e "$device" ] || continue
        total=$((total + 1))
        if [ -r "$device" ]; then
            readable=$((readable + 1))
            name=$(cat "/sys/class/input/$(basename "$device")/device/name" 2>/dev/null || echo '?')
            echo "  可读: $device  $name"
        fi
    done
    echo "会话类型: ${XDG_SESSION_TYPE:-未知}  显示: ${WAYLAND_DISPLAY:-${DISPLAY:-无}}"
    echo "可读输入设备: $readable/$total"
    if [ "$total" -eq 0 ]; then
        echo "没有找到 $dir/event*，确认内核 input 子系统正常。" >&2
        return 1
    fi
    if [ "$readable" -eq 0 ]; then
        echo "还不能读：运行 '$0 uaccess'（推荐）或 '$0 group'。" >&2
        return 1
    fi
    echo "至少有一个设备可读。是否有键盘/鼠标以 fxpet 日志里带「键盘」的行、「evdev: 已接管」为准。"
    echo "（已在运行的 fxpet 会自己在权限到位后接管，不需要重启。）"
}

current_user() {
    if [ "${EUID:-$(id -u)}" -eq 0 ]; then
        echo "${SUDO_USER:-}"
    else
        id -un
    fi
}

case "${1:-check}" in
    uaccess)
        echo "写入 $RULE_PATH ..."
        printf '%s\n' "$RULE_CONTENT" | run_root tee "$RULE_PATH" >/dev/null
        run_root udevadm control --reload-rules
        run_root udevadm trigger --subsystem-match=input --action=change
        run_root udevadm settle || true
        echo "已安装。权限对当前会话立即生效，正在运行的 fxpet 会自动接管；先确认："
        check || true
        ;;
    group)
        user=$(current_user)
        if [ -z "$user" ]; then
            echo "以 root 身份运行无法确定要授权给哪个用户，请用普通用户执行本脚本。" >&2
            exit 1
        fi
        if id -nG "$user" | tr ' ' '\n' | grep -qx input; then
            echo "$user 已在 input 组。"
        else
            run_root usermod -aG input "$user"
            echo "已把 $user 加入 input 组，需要重新登录（或 newgrp input）后生效。"
        fi
        ;;
    check)
        check
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "未知参数: $1" >&2
        usage >&2
        exit 2
        ;;
esac
