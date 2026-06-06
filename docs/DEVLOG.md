# Development Log

本文件用于记录 RK3566 Vision Terminal 项目的每日开发进度、当前问题和下一步计划。

## 2026-06-06

### 今日完成

- 初始化 Git 仓库
- 添加 `.gitignore`
- 上传项目到 GitHub
- 确认 `build/`、`*.swu`、`*.ext4`、日志和可执行文件不会被上传
- 建立基础项目结构
- 提交 `ota_slotctl` 基础代码

### 当前状态

- 当前主分支：`main`
- 当前项目已成功推送到 GitHub
- `ota_slotctl` 已有基础代码和 Makefile
- OTA A/B RootFS 项目处于初始验证阶段

### 当前问题

- `ota_slotctl` 目前还需要继续增强
- 需要进一步支持读取 `misc` 分区中的 A/B 槽位信息
- 需要实现设置 inactive slot 的能力
- SWUpdate OTA 包生成流程还需要脚本化
- 失败回滚策略还未实现

### 下一步

- 创建 `feature/ota-slotctl-misc` 分支
- 在新分支上开发 `misc` 分区读取功能
- 整理 OTA A/B 分区设计文档
- 建立每日开发和提交规范
