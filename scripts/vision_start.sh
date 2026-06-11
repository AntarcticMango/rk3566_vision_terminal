#!/bin/sh

# ============================================================
# Vision Service 启动脚本
#
# 支持三种模式：
# 1. preview：本地屏幕预览
# 2. record ：H.264 编码录制到 /tmp/test.h264
# 3. udp    ：H.264 RTP/UDP 推流到 PC
#
# 示例：
#   ./scripts/vision_start.sh preview
#   ./scripts/vision_start.sh record
#   ./scripts/vision_start.sh udp 192.168.123.100 5000
# ============================================================

# 获取当前脚本所在目录
# 这样即使你不在项目根目录执行，也能找到 vision_status.sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Vision Service 运行时保存的 PID 文件
PID_FILE="/tmp/vision_service.pid"

# 保存当前 Vision Service 运行模式
MODE_FILE="/tmp/vision_service.mode"

# 日志文件
LOG_FILE="/tmp/vision_service.log"

# 默认摄像头节点
DEV="${VISION_DEV:-/dev/video1}"

# 默认 DRM 设备
DRM="${VISION_DRM:-/dev/dri/card0}"

# 第一个参数是运行模式，默认 preview
MODE="${1:-preview}"

# UDP 推流目标 PC IP
PC_IP="$2"

# UDP 推流端口，默认 5000
PORT="${3:-5000}"

# 录制输出文件，默认 /tmp/test.h264
OUT_FILE="${4:-/tmp/test.h264}"

# ------------------------------------------------------------
# 日志函数
# ------------------------------------------------------------
log()
{
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG_FILE"
}

# ------------------------------------------------------------
# 1. 检查摄像头节点是否存在
# ------------------------------------------------------------
if [ ! -e "$DEV" ]; then
    echo "ERROR: camera device $DEV not found"
    log "start failed: camera device $DEV not found"
    exit 1
fi

# ------------------------------------------------------------
# 2. 检查 GStreamer 是否存在
# ------------------------------------------------------------
if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "ERROR: gst-launch-1.0 not found"
    log "start failed: gst-launch-1.0 not found"
    exit 1
fi

# ------------------------------------------------------------
# 3. 检查 Vision Service 是否已经在运行
# ------------------------------------------------------------
if [ -f "$PID_FILE" ]; then
    old_pid="$(cat "$PID_FILE" 2>/dev/null)"

    if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
        echo "vision service already running, pid=$old_pid"
        "$SCRIPT_DIR/vision_status.sh" 2>/dev/null
        exit 0
    else
        # PID 文件存在但进程不存在，清理残留文件
        rm -f "$PID_FILE" "$MODE_FILE"
    fi
fi

# ------------------------------------------------------------
# 4. 根据模式拼接 GStreamer 命令
# ------------------------------------------------------------
case "$MODE" in
    preview)
        # 本地屏幕预览
        #
        # v4l2src：从摄像头采集
        # video/x-raw：指定分辨率、格式和帧率
        # kmssink：直接显示到 DRM/KMS 屏幕
        CMD="gst-launch-1.0 -e \
v4l2src device=$DEV io-mode=mmap \
! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1 \
! queue \
! kmssink sync=false"
        ;;

    record)
        # H.264 录制
        #
        # mpph264enc：Rockchip MPP 硬件 H.264 编码
        # h264parse：整理 H.264 码流
        # filesink：写入文件
        CMD="gst-launch-1.0 -e \
v4l2src device=$DEV io-mode=mmap \
! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1 \
! queue \
! mpph264enc bps=2000000 \
! h264parse \
! filesink location=$OUT_FILE"
        ;;

    udp)
        # RTP/UDP 推流
        #
        # 需要传入 PC_IP
        # 示例：
        #   ./vision_start.sh udp 192.168.123.100 5000
        if [ -z "$PC_IP" ]; then
            echo "Usage: $0 udp <PC_IP> [PORT]"
            echo "Example: $0 udp 192.168.123.100 5000"
            exit 1
        fi

        # rtph264pay：把 H.264 码流打包成 RTP
        # udpsink：通过 UDP 发送到 PC
        CMD="gst-launch-1.0 -e \
v4l2src device=$DEV io-mode=mmap \
! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1 \
! queue \
! mpph264enc bps=2000000 \
! h264parse \
! rtph264pay config-interval=1 pt=96 \
! udpsink host=$PC_IP port=$PORT"
        ;;

    *)
        echo "Usage: $0 {preview|record|udp}"
        echo
        echo "Examples:"
        echo "  $0 preview"
        echo "  $0 record"
        echo "  $0 udp 192.168.123.100 5000"
        exit 1
        ;;
esac

# ------------------------------------------------------------
# 5. 启动 Vision Service
# ------------------------------------------------------------
log "start vision service: mode=$MODE dev=$DEV"
log "cmd: $CMD"

# nohup：让程序在后台运行
# sh -c "$CMD"：执行上面拼接好的 GStreamer 命令
# stdout/stderr 都写入日志文件
nohup sh -c "$CMD" >> "$LOG_FILE" 2>&1 &

# 保存后台进程 PID
pid="$!"
echo "$pid" > "$PID_FILE"
echo "$MODE" > "$MODE_FILE"

# 给进程一点启动时间
sleep 1

# ------------------------------------------------------------
# 6. 检查是否启动成功
# ------------------------------------------------------------
if kill -0 "$pid" 2>/dev/null; then
    echo "vision service started, mode=$MODE, pid=$pid"
    "$SCRIPT_DIR/vision_status.sh"
else
    echo "ERROR: vision service failed to start"
    log "start failed: process exited"

    # 打印最近日志，方便排查问题
    tail -n 30 "$LOG_FILE"

    rm -f "$PID_FILE" "$MODE_FILE"
    exit 1
fi
