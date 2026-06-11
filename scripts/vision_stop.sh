#!/bin/sh

# ============================================================
# Vision Service 停止脚本
#
# 功能：
# 1. 读取 /tmp/vision_service.pid
# 2. 停止正在运行的 Vision Service
# 3. 清理 PID 和模式文件
# 4. 输出停止后的状态
# ============================================================

# 获取当前脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Vision Service PID 文件
PID_FILE="/tmp/vision_service.pid"

# Vision Service 当前模式文件
MODE_FILE="/tmp/vision_service.mode"

# 日志文件
LOG_FILE="/tmp/vision_service.log"

# ------------------------------------------------------------
# 日志函数
# ------------------------------------------------------------
log()
{
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG_FILE"
}

# ------------------------------------------------------------
# 1. 判断 PID 文件是否存在
# ------------------------------------------------------------
if [ ! -f "$PID_FILE" ]; then
    echo "vision service not running"
    log "stop requested: not running"
    exit 0
fi

# 读取 PID
pid="$(cat "$PID_FILE" 2>/dev/null)"

# ------------------------------------------------------------
# 2. 判断进程是否还活着
# ------------------------------------------------------------
if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo "stopping vision service, pid=$pid"
    log "stop vision service: pid=$pid"

    # 先正常结束进程
    kill "$pid" 2>/dev/null
    sleep 1

    # 如果正常 kill 后还没退出，则强制 kill -9
    if kill -0 "$pid" 2>/dev/null; then
        echo "process still alive, force killing..."
        kill -9 "$pid" 2>/dev/null
        log "force killed vision service: pid=$pid"
    fi
else
    # PID 文件存在，但进程已经不存在
    echo "vision service pid not alive"
    log "stop requested: pid not alive"
fi

# ------------------------------------------------------------
# 3. 清理运行状态文件
# ------------------------------------------------------------
rm -f "$PID_FILE" "$MODE_FILE"

echo "vision service stopped"

# ------------------------------------------------------------
# 4. 输出停止后的状态
# ------------------------------------------------------------
"$SCRIPT_DIR/vision_status.sh"
