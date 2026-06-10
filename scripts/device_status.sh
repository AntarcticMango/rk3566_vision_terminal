#!/bin/sh
# Day 2 wrapper: query RK3566 Device Manager status.
# Put device_manager in /usr/bin/device_manager or run this script from project root.

DM_BIN="${DM_BIN:-/usr/bin/device_manager}"
LOCAL_BIN="./src/device_manager/device_manager"
LOG_FILE="/tmp/device_manager.log"

if [ -x "$DM_BIN" ]; then
    "$DM_BIN" --status
elif [ -x "$LOCAL_BIN" ]; then
    "$LOCAL_BIN" --status
else
    echo "{"
    echo "  \"error\": \"device_manager binary not found\","
    echo "  \"hint\": \"build src/device_manager/device_manager.c first\""
    echo "}"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] device_manager binary not found" >> "$LOG_FILE"
    exit 1
fi
