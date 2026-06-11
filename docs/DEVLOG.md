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

## 2026-06-10

### 今日完成

- 完成device_manager.c的文件的编写和验证
- device_manager可以对
```
Device Manager 状态聚合：完成
A/B 槽位识别：完成
WiFi/IP 状态识别：完成
摄像头 online 检测：完成
OTA idle 状态输出：完成
CAN 接口预留：完成
JSON 输出：完成
```
### 当前状态

- 当前分支：
```bash
```
- 当前项目已成功推送到 GitHub


### 当前问题


### 下一步

- 创建 `feature/ota-slotctl-misc` 分支
- 在新分支上开发 `misc` 分区读取功能
- 整理 OTA A/B 分区设计文档
- 建立每日开发和提交规范

## 2026-06-11

### 今日完成

* 完成 DAY4 : Vision Service 演示固定化
* 编写并完善摄像头服务管理脚本
    * `script/vision_start.sh`
    * `script/vision_stop.sh`
    * `script/vision_status.sh`
* 支持摄像头状态检测、视频服务启动/停止/状态查询
* 支持preview/record/udp三种视频模式
* 验证GStreamer HLS链路
    * V4L2 采集
    * MPP H.264 编码
    * MPEG-TS 封装
    * HLS 切片生成
* 在 web_server/rk_web_api.c中新增HLS静态*文件访问能力
* 新增网页视频预览接口：
    * GET /video
    * GET /hls/stream.m3u8
    * GET /hls/segmentxxxxx.ts
* 保留原有 REST API：
    * /api/version
    * /api/ota/status
    * /api/vision/status
    * /api/logs  
* 实现浏览器直接查看摄像头视频流

### 当前状态

* Web API Server 可运行在 8080 端口
* 板端 IP：192.168.123.156
* 摄像头节点 /dev/video1 可用
* HLS 视频流已可通过浏览器访问：

http://192.168.123.156:8080/video

* 当前项目已具备：
    * 设备状态查询
    * OTA 状态查询
    * 摄像头状态查询
    * 日志查看
    * 网页视频预览

### 当前问题

* HLS 视频生成仍需手动执行 gst-launch-1.0
* Vision Service 脚本尚未集成 hls 模式
* /video 页面较简单，暂未做完整 UI
* HLS 播放依赖浏览器端 hls.js
* Web Server 仍是轻量级版本，暂未实现鉴权、HTTPS、线程池等功能
* 视频服务异常退出后的自动恢复机制还未实现

### 下一步

*将 HLS 生成命令封装进 vision_start.sh
*增加 vision_start.sh hls 模式
*完善 /api/vision/status，显示当前视频运行模式
*考虑新增视频控制接口：
    */api/vision/start
    */api/vision/stop
    */api/vision/restart
*编写 docs/04_vision_service.md
*更新 README 中的视频演示流程
*保存网页视频预览截图
*进入 Day5：OTA A/B 演示闭环
