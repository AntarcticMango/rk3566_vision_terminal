
#!/bin/sh

# ============================================================
# Vision Service 状态查询脚本
#
# 功能：
# 1. 检查摄像头节点是否存在
# 2. 检查 Vision Service 是否正在运行
# 3. 输出 JSON 格式状态，方便 Device Manager / Web API 调用
# ============================================================

# Vision Service 运行时保存的 PID 文件
PID_FILE="/tmp/vision_service.pid"

# Vision Service 当前运行模式文件
# 例如：preview / record / udp
MODE_FILE="/tmp/vision_service.mode"

# Vision Service 日志文件
LOG_FILE="/tmp/vision_service.log"

# 默认摄像头节点
# 也可以通过环境变量 VISION_DEV 临时覆盖
# 例如：VISION_DEV=/dev/video0 ./vision_status.sh
DEV="${VISION_DEV:-/dev/video1}"

# 默认 DRM 显示设备
DRM="${VISION_DRM:-/dev/dri/card0}"

# 默认状态
camera="offline"
stream="stopped"
mode="none"
pid="null"

# ------------------------------------------------------------
# 1. 判断摄像头节点是否存在
# [ -e 路劲 ] 是测试文件是否存在的条件表达式
# ------------------------------------------------------------
if [ -e "$DEV" ]; then
    camera="online"
fi

# ------------------------------------------------------------
# 2. 判断 Vision Service 是否正在运行 -f检查 PID 文件是否存在且为普通文件（不是目录等）
# ------------------------------------------------------------
if [ -f "$PID_FILE" ]; then
    pid_val="$(cat "$PID_FILE" 2>/dev/null)"

    # kill -0 不会真正杀进程，只是检查进程是否存在;-n 检查 pid_val 是否非空；如果字符串长度不为零，条件成立。
    if [ -n "$pid_val" ] && kill -0 "$pid_val" 2>/dev/null; then
        stream="running"
        pid="$pid_val"

        # 读取当前运行模式 
#         -z 可以理解为 "zero length"。
# 如果字符串的长度为零（即空串），条件成立。
        mode="$(cat "$MODE_FILE" 2>/dev/null)"
        [ -z "$mode" ] && mode="unknown"
    else
        # PID 文件存在，但进程已经不存在，说明是残留文件
        rm -f "$PID_FILE" "$MODE_FILE"

        stream="stopped"
        mode="none"
        pid="null"
    fi
fi

# ------------------------------------------------------------
# 3. 输出 JSON 状态
# ------------------------------------------------------------
cat <<JSON
{
  "vision": {
    "camera": "$camera",
    "stream": "$stream",
    "mode": "$mode",
    "device": "$DEV",
    "drm": "$DRM",
    "pid": $pid,
    "log": "$LOG_FILE"
  }
}
JSON
