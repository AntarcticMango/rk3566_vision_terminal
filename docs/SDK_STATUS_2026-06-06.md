```bash
ding@ding-VMware-Virtual-Platform:~/tspi/linux$ for d in buildroot device/rockchip u-boot kernel external/rkwifibt external/swupdate; do
    echo "========== $d =========="
    if [ -d "$d/.git" ]; then
        git -C "$d" status --short
    else
        echo "不是 Git 子仓库或目录不存在"
    fi
done
========== buildroot ==========
 M configs/rockchip/rk356x.config
 M configs/rockchip_rk3566_defconfig
?? board/rockchip/common/wifi/etc/
?? board/rockchip/common/wifi/usr/bin/wifi_setup
?? board/rockchip/common/wifi/usr/bin/wifi_status
?? board/rockchip/rk356x/fs-overlay/etc/vision-terminal/
?? board/rockchip/rk356x/fs-overlay/usr/bin/ota_slotctl
?? configs/rockchip_rk3566_defconfig.bak
========== device/rockchip ==========
 M rk356x/BoardConfig-rk3566-tspi-v10.mk
 M rk356x/parameter-buildroot-fit.txt
========== u-boot ==========
刷新索引: 100% (13576/13576), 完成.
 M configs/rk3566.config
========== kernel ==========
 M arch/arm64/boot/dts/rockchip/tspi-rk3566-core-v10.dtsi
 M arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts
 M drivers/input/touchscreen/gt9xx/gt9xx.c
 M drivers/net/ethernet/stmicro/stmmac/Kconfig
 M drivers/net/ethernet/stmicro/stmmac/dwmac-rk-tool.c
 M drivers/net/ethernet/stmicro/stmmac/stmmac.h
 M drivers/net/ethernet/stmicro/stmmac/stmmac_main.c
?? arch/arm64/boot/dts/rockchip/tspi-rk3566-dsi-v10-07255F.dtsi
?? arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-07255F.dts
?? drivers/net/ethernet/stmicro/stmmac_backup_20260520_133110/
========== external/rkwifibt ==========
========== external/swupdate ==========
不是 Git 子仓库或目录不存在
```

# SDK 当前修改状态记录

记录日期：2026-06-06

本文件用于记录当前 Rockchip SDK 中已经发生的修改，避免后续忘记哪些文件被改过、哪些改动需要生成 patch、哪些临时文件需要排除。

## 一、SDK 管理方式

当前 SDK 路径：

```text
~/tspi/linux

该 SDK 顶层不是普通 Git 仓库，而是 repo 多仓库结构：

.repo/
kernel/.git
u-boot/.git
device/rockchip/.git
buildroot/.git
external/...

因此，SDK 修改不能在顶层直接使用 git diff 管理，需要进入具体子仓库分别生成 patch。
```
## 二、当前重点修改子仓库
### 1. buildroot

当前状态：
```bash
 M configs/rockchip/rk356x.config
 M configs/rockchip_rk3566_defconfig
?? board/rockchip/common/wifi/etc/
?? board/rockchip/common/wifi/usr/bin/wifi_setup
?? board/rockchip/common/wifi/usr/bin/wifi_status
?? board/rockchip/rk356x/fs-overlay/etc/vision-terminal/
?? board/rockchip/rk356x/fs-overlay/usr/bin/ota_slotctl
?? configs/rockchip_rk3566_defconfig.bak
```
计划处理：

需要记录：
```bash
configs/rockchip/rk356x.config
configs/rockchip_rk3566_defconfig
board/rockchip/common/wifi/etc/
board/rockchip/common/wifi/usr/bin/wifi_setup
board/rockchip/common/wifi/usr/bin/wifi_status
board/rockchip/rk356x/fs-overlay/etc/vision-terminal/
board/rockchip/rk356x/fs-overlay/usr/bin/ota_slotctl
不记录：
configs/rockchip_rk3566_defconfig.bak
```
原因：

.bak 属于临时备份文件，不应该进入项目仓库。
### 2. device/rockchip

当前状态：
```bash
 M rk356x/BoardConfig-rk3566-tspi-v10.mk
 M rk356x/parameter-buildroot-fit.txt
```
计划处理：

需要记录。
这部分大概率涉及：
BoardConfig 配置
Buildroot 启动配置
A/B RootFS 分区表
system_a / system_b 分区布局
### 3. u-boot

当前状态：
```bash
 M configs/rk3566.config
```
计划处理：

需要记录，但需要先查看具体 diff。
这部分可能与 A/B 启动、AVB、slot suffix 或启动参数相关。

### 4. kernel

当前状态：
```bash
 M arch/arm64/boot/dts/rockchip/tspi-rk3566-core-v10.dtsi
 M arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts
 M drivers/input/touchscreen/gt9xx/gt9xx.c
 M drivers/net/ethernet/stmicro/stmmac/Kconfig
 M drivers/net/ethernet/stmicro/stmmac/dwmac-rk-tool.c
 M drivers/net/ethernet/stmicro/stmmac/stmmac.h
 M drivers/net/ethernet/stmicro/stmmac/stmmac_main.c
?? arch/arm64/boot/dts/rockchip/tspi-rk3566-dsi-v10-07255F.dtsi
?? arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-07255F.dts
?? drivers/net/ethernet/stmicro/stmmac_backup_20260520_133110/
```
计划处理：

需要分组处理，不能全部混在一个 patch 里。
建议拆成：
屏幕/设备树 patch
触摸 gt9xx patch
以太网 stmmac patch
不记录：
drivers/net/ethernet/stmicro/stmmac_backup_20260520_133110/

原因：

stmmac_backup_20260520_133110/ 是备份目录，不应该进入项目仓库。
## 三、建议生成的 patch 文件

后续建议生成以下 patch：
```
patches/sdk/0001-buildroot-swupdate-wifi-ota-overlay.patch
patches/sdk/0002-device-rockchip-ab-partition-config.patch
patches/sdk/0003-uboot-rk3566-config.patch
patches/sdk/0004-kernel-display-touch-dts.patch
patches/sdk/0005-kernel-stmmac-network-modification.patch
```
## 四、处理原则
不上传完整 SDK。
不上传编译产物。
不上传 .bak 文件。
不上传备份目录。
每个 SDK 子仓库单独生成 patch。
每个 patch 对应一个明确功能，不把无关修改混在一起。
修改目的、修改文件、编译命令、验证命令都记录到 docs/SDK_CHANGELOG.md。