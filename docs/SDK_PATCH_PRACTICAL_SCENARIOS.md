# SDK Patch 实际开发场景示例

本文档用于记录在 RK3566 / Rockchip SDK 开发过程中常见的 patch 使用场景，包括：

- 如何记录 SDK 修改
- 如何生成 patch
- 如何应用 patch
- 如何回退 patch
- 如何处理 patch 冲突
- 如何避免把无关文件打进 patch

本项目涉及两个目录：

```text
~/tspi/rk3566_vision_terminal   项目仓库，上传 GitHub
~/tspi/linux                    Rockchip SDK，本地编译工作区，不整体上传 GitHub
```

核心原则：

```text
项目源码放进 GitHub。
完整 SDK 不上传。
SDK 修改通过 patch 记录。
功能稳定后再归档 patch。
临时试验不急着 patch。
```

---

# 场景 1：修改 Buildroot 配置后，记录成 patch

## 场景说明

你为了支持 SWUpdate OTA，修改了 Buildroot 配置，例如：

```text
~/tspi/linux/buildroot/configs/rockchip_rk3566_defconfig
~/tspi/linux/buildroot/configs/rockchip/rk356x.config
```

这些文件属于 Rockchip SDK，不在你的项目仓库里。  
所以需要把 SDK 修改导出成 patch，放到项目仓库：

```text
~/tspi/rk3566_vision_terminal/patches/sdk/
```

## 操作步骤

进入 Buildroot 子仓库：

```bash
cd ~/tspi/linux/buildroot
```

先查看修改状态：

```bash
git status --short
git diff --stat
```

如果确认只想记录这两个配置文件，执行：

```bash
git diff -- \
  configs/rockchip_rk3566_defconfig \
  configs/rockchip/rk356x.config \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0006-buildroot-swupdate-config.patch
```

回到项目仓库检查 patch：

```bash
cd ~/tspi/rk3566_vision_terminal

ls -lh patches/sdk/0006-buildroot-swupdate-config.patch
head -40 patches/sdk/0006-buildroot-swupdate-config.patch
```

确认没有空 patch：

```bash
find patches/sdk -name "*.patch" -size 0 -print
```

提交到 GitHub：

```bash
git add patches/sdk/0006-buildroot-swupdate-config.patch
git commit -m "patch(sdk): record buildroot swupdate config"
git push
```

## 注意事项

不要直接：

```bash
git diff > xxx.patch
```

除非你确认当前 Buildroot 里所有修改都属于这个功能。

更安全的做法是指定文件路径：

```bash
git diff -- file1 file2 > xxx.patch
```

---

# 场景 2：新增 WiFi 脚本后，记录成 patch

## 场景说明

你新增了 WiFi 自动连接脚本：

```text
~/tspi/linux/buildroot/board/rockchip/common/wifi/usr/bin/wifi_setup
~/tspi/linux/buildroot/board/rockchip/common/wifi/etc/init.d/S37wifi_auto
```

这些是 SDK 里的新文件，`git status` 会显示：

```text
?? board/rockchip/common/wifi/usr/bin/wifi_setup
?? board/rockchip/common/wifi/etc/init.d/S37wifi_auto
```

新文件如果直接 `git diff`，可能不会进入 patch，所以需要用 `git add -N`。

## 操作步骤

进入 Buildroot：

```bash
cd ~/tspi/linux/buildroot
```

让 Git 识别这些新文件，但不真正提交：

```bash
git add -N \
  board/rockchip/common/wifi/usr/bin/wifi_setup \
  board/rockchip/common/wifi/etc/init.d/S37wifi_auto
```

生成 patch：

```bash
git diff -- \
  board/rockchip/common/wifi/usr/bin/wifi_setup \
  board/rockchip/common/wifi/etc/init.d/S37wifi_auto \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0007-buildroot-wifi-autostart.patch
```

取消暂存状态：

```bash
git reset -- \
  board/rockchip/common/wifi/usr/bin/wifi_setup \
  board/rockchip/common/wifi/etc/init.d/S37wifi_auto
```

回到项目仓库提交：

```bash
cd ~/tspi/rk3566_vision_terminal

git add patches/sdk/0007-buildroot-wifi-autostart.patch
git commit -m "patch(sdk): add wifi autostart scripts"
git push
```

## 注意事项

`git add -N` 的作用是：

```text
让新文件进入 git diff 的观察范围，但不真正提交 SDK。
```

它适合新增文件生成 patch 的场景。

---

# 场景 3：修改设备树后，记录成 kernel patch

## 场景说明

你为了适配屏幕、触摸或者外设，修改了设备树文件，例如：

```text
~/tspi/linux/kernel/arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts
~/tspi/linux/kernel/arch/arm64/boot/dts/rockchip/tspi-rk3566-core-v10.dtsi
```

这些修改属于 kernel 子仓库，应该在 kernel 目录下生成 patch。

## 操作步骤

进入 kernel：

```bash
cd ~/tspi/linux/kernel
```

查看修改：

```bash
git status --short
git diff --stat
```

生成指定文件 patch：

```bash
git diff -- \
  arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts \
  arch/arm64/boot/dts/rockchip/tspi-rk3566-core-v10.dtsi \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0008-kernel-device-tree-update.patch
```

回项目仓库检查：

```bash
cd ~/tspi/rk3566_vision_terminal

ls -lh patches/sdk/0008-kernel-device-tree-update.patch
grep -n "diff --git" patches/sdk/0008-kernel-device-tree-update.patch
```

提交：

```bash
git add patches/sdk/0008-kernel-device-tree-update.patch
git commit -m "patch(sdk): record kernel device tree update"
git push
```

## 注意事项

如果 kernel 里有很多历史修改，不要直接：

```bash
git diff > patch.patch
```

因为它会把所有 kernel 修改都打进去。

推荐永远指定文件路径。

---

# 场景 4：应用 patch 到干净 SDK

## 场景说明

你重新解压了一份干净 SDK，或者换了一台电脑，需要把之前 GitHub 项目里的 SDK patch 应用回 SDK。

比如要应用：

```text
patches/sdk/0002-device-rockchip-ab-partition-config.patch
```

这个 patch 是给：

```text
~/tspi/linux/device/rockchip
```

用的。

## 操作步骤

进入对应 SDK 子仓库：

```bash
cd ~/tspi/linux/device/rockchip
```

检查当前状态：

```bash
git status --short
```

先检查 patch 能不能应用：

```bash
git apply --check ~/tspi/rk3566_vision_terminal/patches/sdk/0002-device-rockchip-ab-partition-config.patch
```

如果没有输出，说明可以应用。

正式应用：

```bash
git apply ~/tspi/rk3566_vision_terminal/patches/sdk/0002-device-rockchip-ab-partition-config.patch
```

查看应用结果：

```bash
git status --short
git diff --stat
```

## 注意事项

patch 必须在对应子仓库里应用。

错误示例：

```bash
cd ~/tspi/linux
git apply ~/tspi/rk3566_vision_terminal/patches/sdk/0002-device-rockchip-ab-partition-config.patch
```

正确示例：

```bash
cd ~/tspi/linux/device/rockchip
git apply ~/tspi/rk3566_vision_terminal/patches/sdk/0002-device-rockchip-ab-partition-config.patch
```

---

# 场景 5：patch 应用错了，如何回退

## 场景说明

你应用了一个 patch，后来发现不应该应用，或者应用到了错误 SDK 版本上。

如果 patch 只是 `git apply` 了，还没有在 SDK 子仓库里 commit，可以用反向 patch 回退。

## 操作步骤

假设要撤销 Buildroot patch：

```text
0001-buildroot-swupdate-wifi-overlay.patch
```

进入 Buildroot：

```bash
cd ~/tspi/linux/buildroot
```

先检查是否可以反向撤销：

```bash
git apply -R --check ~/tspi/rk3566_vision_terminal/patches/sdk/0001-buildroot-swupdate-wifi-overlay.patch
```

如果没有输出，正式撤销：

```bash
git apply -R ~/tspi/rk3566_vision_terminal/patches/sdk/0001-buildroot-swupdate-wifi-overlay.patch
```

查看状态：

```bash
git status --short
```

## 注意事项

`-R` 的意思是 reverse，也就是反向应用 patch。

如果 patch 已经被你手动改过、部分应用过、或者文件又被修改了，反向撤销可能失败。

这时候不要强行操作，先看：

```bash
git status --short
git diff --stat
```

---

# 场景 6：patch 应用失败，如何处理

## 场景说明

你执行：

```bash
git apply --check xxx.patch
```

结果报错，例如：

```text
error: patch failed: configs/xxx:10
error: configs/xxx: patch does not apply
```

这说明当前 SDK 文件内容和生成 patch 时的版本不一致。

常见原因：

```text
SDK 版本不一样
这个文件已经被你改过
patch 已经应用过一次
文件路径不对
进入了错误的 SDK 子仓库
```

## 处理步骤

### 1. 确认自己在哪个目录

```bash
pwd
```

比如 buildroot patch 应该在：

```text
~/tspi/linux/buildroot
```

kernel patch 应该在：

```text
~/tspi/linux/kernel
```

### 2. 确认当前修改状态

```bash
git status --short
git diff --stat
```

如果文件已经被修改过，可能会冲突。

### 3. 检查 patch 修改了哪些文件

```bash
grep -n "diff --git" ~/tspi/rk3566_vision_terminal/patches/sdk/xxx.patch
```

例如输出：

```text
diff --git a/configs/rockchip_rk3566_defconfig b/configs/rockchip_rk3566_defconfig
```

说明这个 patch 应该在 buildroot 子仓库里应用。

### 4. 如果确认 patch 已经应用过

可以看目标文件是否已经有对应内容：

```bash
grep -n "SWUPDATE" configs/rockchip_rk3566_defconfig
```

如果内容已经存在，说明不用重复应用。

### 5. 如果确实冲突

不要强行应用。可以选择：

```text
手动对照 patch 修改文件
重新基于当前 SDK 生成新的 patch
或者先恢复 SDK 到干净状态再应用
```

---

# 场景 7：避免把二进制文件打进 patch

## 场景说明

你之前遇到过：

```text
board/rockchip/rk356x/fs-overlay/usr/bin/ota_slotctl
```

它是 ARM64 ELF 可执行文件：

```text
ELF 64-bit LSB executable, ARM aarch64
```

这种文件属于构建产物，不应该进入 patch。

## 检查方法

```bash
file ~/tspi/linux/buildroot/board/rockchip/rk356x/fs-overlay/usr/bin/ota_slotctl
```

如果输出包含：

```text
ELF
```

就不要纳入 patch。

## 错误做法

```bash
cd ~/tspi/linux/buildroot
git diff > ~/tspi/rk3566_vision_terminal/patches/sdk/ota.patch
```

如果当前 SDK 里有这个二进制，很可能把它打进去。

## 正确做法

只指定文本文件生成 patch：

```bash
cd ~/tspi/linux/buildroot

git diff -- \
  configs/rockchip_rk3566_defconfig \
  configs/rockchip/rk356x.config \
  board/rockchip/rk356x/fs-overlay/etc/vision-terminal/version \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0009-buildroot-text-config-only.patch
```

## 归档前检查

```bash
cd ~/tspi/rk3566_vision_terminal

grep -RIn "Binary files\|ota_slotctl" patches/sdk
```

如果出现：

```text
Binary files ...
diff --git a/.../ota_slotctl
```

说明 patch 不干净，需要重新生成。

---

# 场景 8：把混在一起的 SDK 修改拆成多个 patch

## 场景说明

你在 kernel 里同时改了：

```text
设备树
触摸驱动
网卡 stmmac
```

如果直接：

```bash
git diff > kernel-all.patch
```

这个 patch 会很乱。

更好的做法是按功能拆分：

```text
kernel-display-touch.patch
kernel-stmmac-network.patch
```

## 操作示例

### 1. 生成显示和触摸 patch

```bash
cd ~/tspi/linux/kernel

git diff -- \
  arch/arm64/boot/dts/rockchip/tspi-rk3566-core-v10.dtsi \
  arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts \
  drivers/input/touchscreen/gt9xx/gt9xx.c \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0010-kernel-display-touch.patch
```

### 2. 生成 stmmac 网络 patch

```bash
cd ~/tspi/linux/kernel

git diff -- \
  drivers/net/ethernet/stmicro/stmmac/Kconfig \
  drivers/net/ethernet/stmicro/stmmac/dwmac-rk-tool.c \
  drivers/net/ethernet/stmicro/stmmac/stmmac.h \
  drivers/net/ethernet/stmicro/stmmac/stmmac_main.c \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0011-kernel-stmmac-network.patch
```

## 好处

这样以后别人看到 patch 名就知道：

```text
这个 patch 是干什么的
影响哪些模块
出了问题该回退哪个 patch
```

---

# 场景 9：功能分支里同步 SDK 修改

## 场景说明

你在项目仓库开了功能分支：

```text
feature/ota-slotctl-misc
```

这个功能既改项目源码：

```text
services/ota_service/ota_slotctl/ota_slotctl.c
```

又改 SDK：

```text
buildroot config
kernel DTS
device/rockchip 分区表
```

项目分支不会自动管理 SDK 修改，所以你需要把 SDK 修改生成 patch 放回当前 feature 分支。

## 操作步骤

### 1. 创建项目功能分支

```bash
cd ~/tspi/rk3566_vision_terminal

git switch main
git pull
git switch -c feature/ota-slotctl-misc
```

### 2. 修改项目源码和 SDK

正常修改：

```text
services/ota_service/ota_slotctl/ota_slotctl.c
~/tspi/linux/buildroot/...
~/tspi/linux/kernel/...
```

### 3. 功能稳定后生成 patch

例如 Buildroot 修改：

```bash
cd ~/tspi/linux/buildroot

git diff -- \
  configs/rockchip_rk3566_defconfig \
  configs/rockchip/rk356x.config \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0012-buildroot-ota-slotctl-misc.patch
```

例如 kernel 修改：

```bash
cd ~/tspi/linux/kernel

git diff -- \
  arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0013-kernel-ota-slotctl-misc.patch
```

### 4. 回项目仓库提交

```bash
cd ~/tspi/rk3566_vision_terminal

git add services/ota_service/ota_slotctl docs patches
git commit -m "feat(slotctl): add misc parser and related sdk patches"
git push -u origin feature/ota-slotctl-misc
```

这样这个 feature 分支就同时记录了：

```text
项目源码修改
SDK 修改 patch
开发文档
```

---

# 场景 10：临时实验失败，不生成正式 patch

## 场景说明

你尝试修改 u-boot 或 DTS，但验证失败了，比如：

```text
系统无法启动
屏幕不亮
触摸不工作
网络异常
```

这种修改不应该生成正式 patch。

## 推荐做法

记录到开发日志：

```text
docs/DEVLOG.md
```

示例：

```markdown
## 2026-06-07

### 今日尝试

- 尝试修改 u-boot config 支持 A/B slot 切换。
- 修改后系统启动异常，暂不归档为正式 patch。

### 当前结论

- 该方案暂时不可用。
- 需要继续确认 rk_avb_get_current_slot 和 misc 分区关系。

### 下一步

- 回退 u-boot config 临时修改。
- 先从 ota_slotctl 读取 misc 分区入手。
```

如果要撤销 SDK 临时修改：

```bash
cd ~/tspi/linux/u-boot

git status --short
git restore configs/rk3566.config
```

如果有新增临时文件，手动删除或使用：

```bash
rm path/to/temp_file
```

## 注意事项

失败尝试也值得记录，但不一定值得生成 patch。

推荐规则：

```text
验证成功的 SDK 修改，生成正式 patch。
验证失败的 SDK 修改，写进 DEVLOG。
```

---

# 场景 11：patch 已经生成，但后来又改了同一功能

## 场景说明

你已经生成了：

```text
0012-buildroot-ota-slotctl-misc.patch
```

后来又继续修改同一个功能的 Buildroot 配置。

这时候有两种处理方式。

## 方式 A：覆盖原 patch

适合 patch 还没有合并进 main，或者还只是当前 feature 分支内部使用。

```bash
cd ~/tspi/linux/buildroot

git diff -- \
  configs/rockchip_rk3566_defconfig \
  configs/rockchip/rk356x.config \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0012-buildroot-ota-slotctl-misc.patch
```

然后提交：

```bash
cd ~/tspi/rk3566_vision_terminal

git add patches/sdk/0012-buildroot-ota-slotctl-misc.patch
git commit -m "patch(sdk): update buildroot ota slotctl patch"
git push
```

## 方式 B：新增一个 patch

适合旧 patch 已经进入 main，或者你想保留历史：

```text
0012-buildroot-ota-slotctl-misc.patch
0014-buildroot-ota-slotctl-misc-fix.patch
```

提交：

```bash
git add patches/sdk/0014-buildroot-ota-slotctl-misc-fix.patch
git commit -m "patch(sdk): fix buildroot ota slotctl config"
git push
```

## 推荐规则

```text
feature 分支内：可以覆盖同一个 patch。
合并 main 后：尽量新增 patch，保留历史。
```

---

# 场景 12：想确认 patch 到底改了什么

## 场景说明

你过几天忘了某个 patch 是干嘛的，可以直接查看 patch 内容。

## 常用命令

查看 patch 修改了哪些文件：

```bash
grep -n "diff --git" patches/sdk/0004-kernel-display-touch-dts.patch
```

查看前 80 行：

```bash
head -80 patches/sdk/0004-kernel-display-touch-dts.patch
```

搜索关键字：

```bash
grep -n "gt9xx\|dsi\|07255F" patches/sdk/0004-kernel-display-touch-dts.patch
```

统计文件大小：

```bash
ls -lh patches/sdk
```

## 推荐写法

每次新增 patch 后，在 `docs/SDK_CHANGELOG.md` 里说明：

```markdown
## 2026-06-07 - 新增 kernel display/touch patch

### 修改目的

适配 07255F MIPI-DSI 屏幕和 GT9xx 触摸。

### 对应 patch

```text
patches/sdk/0004-kernel-display-touch-dts.patch
```

### 验证结果

屏幕显示正常，触摸驱动加载正常。
```

---

# 总结

patch 可以理解为：

```text
一份 SDK 修改说明书
```

它记录：

```text
哪些文件变了
哪些行增加了
哪些行删除了
新文件是什么
旧文件怎么改
```

日常开发中推荐：

```text
临时实验：只写 DEVLOG，不生成正式 patch
功能稳定：生成 patch
patch 生成后：检查是否包含脏文件
patch 归档后：提交到项目仓库
新环境复现：进入对应 SDK 子仓库 git apply
应用错了：git apply -R 回退
```

核心口诀：

```text
项目源码进 Git。
完整 SDK 不进 Git。
SDK 修改变 patch。
临时试验写日志。
稳定功能再归档。
应用之前先 --check。
回退 patch 用 -R。
```
