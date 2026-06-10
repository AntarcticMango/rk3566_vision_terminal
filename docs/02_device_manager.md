# Day 2：Device Manager 设计与实现

## 1. 目标

Device Manager 是 RK3566 智能视觉终端的设备状态聚合中心。它的作用不是替代 Vision、OTA、CAN 等模块，而是把这些模块的运行状态统一收集起来，对上输出稳定的 JSON 数据。

Day 2 的最小闭环目标：

```bash
device_manager --status
```

输出类似：

```json
{
  "device": "RK3566-TaishanPi",
  "version": "dev-day2",
  "system": {
    "uptime": "1234s",
    "cpu_temp_c": "52.3",
    "memory_used": "320MB",
    "memory_total": "1900MB",
    "rootfs": "system_a"
  },
  "network": {
    "wlan0_ip": "192.168.1.88",
    "wifi": "connected"
  },
  "vision": {
    "camera": "online",
    "stream": "running"
  },
  "ota": {
    "current_slot": "_a",
    "inactive_slot": "_b",
    "status": "idle",
    "rollback_available": true
  },
  "can": {
    "interface": "can0",
    "status": "reserved"
  }
}
```

---

## 2. 模块职责

| 模块 | 采集方式 | 输出字段 |
|---|---|---|
| 系统状态 | `/proc/uptime`、`/proc/meminfo`、`/sys/class/thermal` | uptime、温度、内存 |
| RootFS 状态 | `/proc/mounts`、`/proc/cmdline`、`/dev/block/by-name` | rootfs、current_slot、inactive_slot |
| 网络状态 | `ip addr show wlan0` 或 `ifconfig wlan0` | wlan0_ip、wifi |
| 摄像头状态 | `/dev/video*` | camera |
| 视频流状态 | `pgrep` 检测预览/推流进程 | stream |
| OTA 状态 | `/tmp/ota_status` | idle/upgrading/failed |
| CAN 状态 | `/sys/class/net/can0` 或 `vcan0` | interface、status |

---

## 3. 编译方法

### 3.1 在板端直接编译

如果 Buildroot 镜像里带了 gcc：

```bash
cd /work/rk3566_vision_terminal/src/device_manager
gcc -O2 -Wall -Wextra -o device_manager device_manager.c
```

### 3.2 在 SDK / Docker 里交叉编译

如果工具链前缀是 `aarch64-buildroot-linux-gnu-`：

```bash
cd /work/rk3566_vision_terminal/src/device_manager
make CROSS_COMPILE=aarch64-buildroot-linux-gnu-
```

如果 Makefile 没接收 `CROSS_COMPILE`，可以直接指定：

```bash
make CC=aarch64-buildroot-linux-gnu-gcc
```

也可以直接编译：

```bash
aarch64-buildroot-linux-gnu-gcc -O2 -Wall -Wextra \
  -o device_manager device_manager.c
```

---

## 4. 部署方法

把二进制放到板端：

```bash
adb push device_manager /usr/bin/device_manager
adb shell chmod +x /usr/bin/device_manager
```

或者通过 SSH：

```bash
scp device_manager root@板子IP:/usr/bin/device_manager
ssh root@板子IP "chmod +x /usr/bin/device_manager"
```

---

## 5. 运行测试

### 5.1 查询总状态

```bash
device_manager --status
```

### 5.2 通过脚本查询

```bash
./scripts/device_status.sh
```

### 5.3 查看日志

```bash
cat /tmp/device_manager.log
```

---

## 6. 调试检查命令

如果某个字段不正确，按下面顺序检查。

### 槽位识别

```bash
cat /proc/cmdline
mount | grep ' / '
ls -l /dev/block/by-name/
```

重点看：

```text
android_slotsufix=_a
system_a -> ../../mmcblk0p6
system_b -> ../../mmcblk0p7
```

### 网络状态

```bash
ip addr show wlan0
ifconfig wlan0
```

### 摄像头状态

```bash
ls /dev/video*
v4l2-ctl --list-devices
```

### 视频流状态

```bash
ps | grep -E 'rk_camera_preview_mplane|gst-launch|mpph264enc'
```

### CAN 状态

```bash
ls /sys/class/net
cat /sys/class/net/can0/operstate 2>/dev/null
cat /sys/class/net/vcan0/operstate 2>/dev/null
```

---

## 7. Day 2 完成标准

完成后应满足：

```text
1. device_manager --status 能输出 JSON。
2. JSON 中能看到 system/network/vision/ota/can 字段。
3. 能识别当前槽位 _a / _b。
4. 能判断 inactive slot。
5. 能判断 wlan0 是否连接。
6. 能判断摄像头节点是否存在。
7. 每次查询都会写入 /tmp/device_manager.log。
```

---

## 8. 面试讲法

可以这样讲：

> 我没有把摄像头、OTA、网络状态散落在多个脚本里，而是抽象了一个 Device Manager 作为设备状态聚合层。它通过读取 `/proc`、`/sys`、设备节点和运行进程状态，统一输出 JSON。后续 Web Server 只需要调用这个模块，就可以对外提供设备状态查询 API。

