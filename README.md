# RK3566 Vision Terminal

基于 RK3566 / 泰山派的嵌入式 Linux 项目，当前主要用于验证 Buildroot 系统下的 SWUpdate OTA、A/B RootFS 分区切换与槽位检测功能。

## 当前功能

- Buildroot 系统定制
- SWUpdate OTA 包构建
- A/B RootFS 分区方案验证
- ota_slotctl 槽位检测工具
- 后续计划加入 OTA 切换、失败回滚、WiFi 自动连接等功能

## 项目结构

```text
configs/    配置文件
docs/       项目文档
scripts/    构建和部署脚本
services/   板端服务程序