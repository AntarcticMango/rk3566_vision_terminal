# SDK 基线说明

本文件用于记录 RK3566 Vision Terminal 项目使用的厂商 SDK 基线信息。

## 一、平台信息

- 开发板：泰山派 RK3566 V10
- SoC：Rockchip RK3566
- 系统类型：Buildroot
- SDK 包：tspi_linux_sdk_repo_20240131.tar.gz
- SDK 工作目录：/work/linux
- 编译环境：Docker Ubuntu 18.04
- 项目仓库目录：~/tspi/rk3566_vision_terminal

## 二、SDK 编译入口

cd /work/linux
./build.sh lunch
./build.sh all
./mkfirmware.sh
./build.sh updateimg

## 三、管理原则

本项目不上传完整厂商 SDK 到 GitHub。

GitHub 仓库只管理以下内容：

项目源码
SDK 修改补丁
关键配置文件快照
构建脚本
验证文档
开发日志

## 四、重点关注的 SDK 文件

以下 SDK 文件如果被修改，需要用 patch、配置快照或文档记录：

buildroot/configs/rockchip_rk3566_defconfig
buildroot/package/swupdate/swupdate.config
buildroot/board/rockchip/common/
device/rockchip/
u-boot/
kernel/
## 五、不上传的内容

以下内容属于构建产物，不提交到 GitHub：
```bash
rootfs.ext4
update.img
*.swu
*.o
*.ko
*.bin
build/
logs/
```