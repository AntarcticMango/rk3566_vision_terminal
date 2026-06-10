# Git / SDK Patch 定期执行命令清单

本文档用于记录 `rk3566_vision_terminal` 项目的日常 Git 使用流程、SDK 修改检查流程、每周整理流程和阶段性归档流程。

项目目录：

```text
~/tspi/rk3566_vision_terminal   项目仓库，上传 GitHub
~/tspi/linux                    Rockchip SDK，本地编译工作区，不整体上传 GitHub
```

核心原则：

```text
每天：保存进度，记录状态。
每周：整理文档，检查 SDK 修改是否失控。
每阶段：归档 patch，打 tag，合并 main。
```

---

# 一、每天开始开发前执行

每天开始前，先确认自己在哪个分支、代码是否干净、本地是否和 GitHub 同步。

## 1. 进入项目仓库

```bash
cd ~/tspi/rk3566_vision_terminal
```

## 2. 查看当前状态

```bash
git status
git branch -vv
git log --oneline --decorate -5
```

需要关注：

```text
当前在哪个分支
有没有未提交文件
当前分支是否跟踪 origin/xxx
最近一次提交是什么
```

## 3. 更新远程分支信息

```bash
git fetch --all --prune
```

作用：

```text
同步 GitHub 上最新分支信息
清理本地已经失效的远程分支记录
```

## 4. 拉取当前分支最新内容

如果当前在 `main`：

```bash
git switch main
git pull
```

如果当前在某个 feature 分支：

```bash
git pull
```

## 5. 如果要开新功能分支

例如开发 `ota_slotctl` 的 misc 分区解析：

```bash
git switch main
git pull
git switch -c feature/ota-slotctl-misc
```

第一次推送新分支：

```bash
git push -u origin feature/ota-slotctl-misc
```

---

# 二、每天开发结束后执行

每天结束前必须保存当前进度。  
如果当天只改项目仓库，不改 SDK，执行“普通收工流程”。  
如果当天改了 SDK，还要执行“SDK 收工检查流程”。

---

## 情况 A：当天只改项目仓库

适用场景：

```text
修改 services/
修改 docs/
修改 scripts/
修改 README.md
没有改 ~/tspi/linux 里的 SDK
```

### 1. 查看项目仓库状态

```bash
cd ~/tspi/rk3566_vision_terminal

git status
git diff --stat
```

### 2. 查看具体改动

如果想看具体内容：

```bash
git diff
```

如果只想看最近提交：

```bash
git log --oneline --decorate -5
```

### 3. 更新 DEVLOG

打开开发日志：

```bash
nano docs/DEVLOG.md
```

追加当天记录，建议格式：

```markdown
## YYYY-MM-DD

### 今日完成

- 

### 当前问题

- 

### 下一步

- 
```

### 4. 提交当天进度

如果是完整功能：

```bash
git add .
git commit -m "feat(module): describe what changed"
git push
```

如果只是临时保存进度：

```bash
git add .
git commit -m "wip: update development progress"
git push
```

---

## 情况 B：当天修改过 SDK

适用场景：

```text
改过 ~/tspi/linux/buildroot
改过 ~/tspi/linux/kernel
改过 ~/tspi/linux/u-boot
改过 ~/tspi/linux/device/rockchip
```

### 1. 回到项目仓库

```bash
cd ~/tspi/rk3566_vision_terminal
```

### 2. 执行 SDK 状态检查脚本

```bash
./scripts/check_sdk_status.sh
```

如果脚本没有执行权限：

```bash
chmod +x scripts/check_sdk_status.sh
./scripts/check_sdk_status.sh
```

### 3. 把重要输出记录到 DEVLOG

打开：

```bash
nano docs/DEVLOG.md
```

记录格式建议：

```markdown
## YYYY-MM-DD

### 今日完成

- 

### SDK 修改状态

```text
buildroot:


device/rockchip:


u-boot:


kernel:

```

### 当前问题

- 

### 下一步

- 
```

### 4. 检查项目仓库状态

```bash
git status
git diff --stat
```

### 5. 提交当天记录

如果只是记录 SDK 状态，还没有生成正式 patch：

```bash
git add docs/DEVLOG.md docs/SDK_CHANGELOG.md
git commit -m "wip: update sdk development record"
git push
```

如果当天也改了项目源码：

```bash
git add .
git commit -m "wip: update project and sdk development record"
git push
```

---

# 三、每天结束前的安全检查

每天关机或收工前，建议最后执行一次：

```bash
cd ~/tspi/rk3566_vision_terminal

git status
git branch -vv
```

理想结果：

```text
工作区干净
当前分支已经 push 到远程
```

如果看到：

```text
您的分支领先 'origin/xxx' 共 1 个提交
```

说明本地有提交没推送，执行：

```bash
git push
```

如果看到：

```text
您的分支落后 'origin/xxx'
```

说明远程有更新，本地没有，执行：

```bash
git pull
```

如果看到：

```text
您的分支和 'origin/xxx' 出现了偏离
```

一般执行：

```bash
git pull --rebase
git push
```

---

# 四、每周执行一次的命令

每周整理一次项目，防止分支、日志、SDK 修改越来越乱。

---

## 1. 进入项目仓库

```bash
cd ~/tspi/rk3566_vision_terminal
```

## 2. 更新远程信息

```bash
git fetch --all --prune
```

## 3. 查看所有分支

```bash
git branch -a
git branch -vv
```

检查是否有已经合并但未删除的分支。

## 4. 查看提交历史图

```bash
git log --oneline --decorate --graph --all -20
```

如果历史是一条直线，说明最近都是 fast-forward 合并，是正常的。

## 5. 执行 SDK 状态检查

```bash
./scripts/check_sdk_status.sh
```

重点看：

```text
buildroot 是否还有未归档修改
kernel 是否还有历史遗留修改
u-boot 是否有未记录修改
device/rockchip 是否有分区表或 BoardConfig 修改
是否出现 .bak / backup / ELF 二进制
```

## 6. 检查 patch 目录

```bash
ls -lh patches/sdk
find patches/sdk -name "*.patch" -size 0 -print
grep -RIn "defconfig.bak\|backup\|Binary files\|ota_slotctl" patches/sdk
```

要求：

```text
不能有空 patch
不能包含 .bak
不能包含 backup 目录
不能包含 ELF 二进制
不能把 ota_slotctl 二进制打进 patch
```

## 7. 检查文档是否需要更新

```bash
ls docs
```

建议每周至少检查这些文件：

```text
docs/DEVLOG.md
docs/TODO.md
docs/SDK_CHANGELOG.md
docs/SDK_BASELINE.md
docs/SDK_STATUS_*.md
docs/PATCH_USAGE_AND_BRANCH_WORKFLOW.md
docs/SDK_PATCH_PRACTICAL_SCENARIOS.md
```

## 8. 更新 TODO

```bash
nano docs/TODO.md
```

检查：

```text
哪些任务完成了，需要改成 [x]
哪些任务已经过时，需要删除或改写
下一阶段要做什么
```

## 9. 每周整理提交

如果有文档更新：

```bash
git add docs README.md
git commit -m "docs: update weekly project records"
git push
```

如果没有任何变化，不需要强行提交。

---

# 五、每个功能阶段完成后执行

阶段完成指的是：某个功能已经验证成功，可以作为一个稳定节点保存。

例如：

```text
v0.1.0  项目初始化与 SDK patch 归档
v0.2.0  ota_slotctl 当前槽位检测
v0.3.0  misc 分区解析
v0.4.0  A/B 槽位切换
v0.5.0  SWUpdate 写入 inactive slot
```

---

## 1. 确认当前功能分支状态

假设当前在：

```text
feature/ota-slotctl-misc
```

执行：

```bash
cd ~/tspi/rk3566_vision_terminal

git status
git branch -vv
git log --oneline --decorate -5
```

确保当前修改都已经提交。

---

## 2. 如果该阶段修改了 SDK，生成 patch

### Buildroot patch 示例

```bash
cd ~/tspi/linux/buildroot

git status --short
git diff --stat

git diff -- \
  configs/rockchip_rk3566_defconfig \
  configs/rockchip/rk356x.config \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0006-buildroot-feature-name.patch
```

### Kernel patch 示例

```bash
cd ~/tspi/linux/kernel

git status --short
git diff --stat

git diff -- \
  arch/arm64/boot/dts/rockchip/xxx.dts \
  arch/arm64/boot/dts/rockchip/xxx.dtsi \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0007-kernel-feature-name.patch
```

### device/rockchip patch 示例

```bash
cd ~/tspi/linux/device/rockchip

git status --short
git diff --stat

git diff -- \
  rk356x/BoardConfig-rk3566-tspi-v10.mk \
  rk356x/parameter-buildroot-fit.txt \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0008-device-feature-name.patch
```

### u-boot patch 示例

```bash
cd ~/tspi/linux/u-boot

git status --short
git diff --stat

git diff -- \
  configs/rk3566.config \
  > ~/tspi/rk3566_vision_terminal/patches/sdk/0009-uboot-feature-name.patch
```

---

## 3. 检查 patch 是否干净

回项目仓库：

```bash
cd ~/tspi/rk3566_vision_terminal
```

检查空 patch：

```bash
find patches/sdk -name "*.patch" -size 0 -print
```

检查脏内容：

```bash
grep -RIn "defconfig.bak\|backup\|Binary files\|ota_slotctl" patches/sdk
```

查看本次新增 patch 修改了哪些文件：

```bash
grep -n "diff --git" patches/sdk/0006-buildroot-feature-name.patch
```

---

## 4. 更新 SDK_CHANGELOG

```bash
nano docs/SDK_CHANGELOG.md
```

追加格式：

```markdown
## YYYY-MM-DD - 阶段名称

### 一、修改目的

说明本阶段为什么修改 SDK。

### 二、修改的 SDK 子仓库

```text
buildroot
kernel
device/rockchip
u-boot
```

### 三、对应 patch

```text
patches/sdk/0006-buildroot-feature-name.patch
patches/sdk/0007-kernel-feature-name.patch
```

### 四、验证命令

```bash
# 板端验证命令
```

### 五、验证结果

说明是否成功，是否还有问题。
```

---

## 5. 更新 DEVLOG

```bash
nano docs/DEVLOG.md
```

记录：

```markdown
## YYYY-MM-DD

### 今日完成

- 完成 xxx 阶段功能
- 生成 SDK patch
- 完成板端验证

### 对应 patch

```text
patches/sdk/xxxx.patch
```

### 验证结果

- 

### 下一步

- 
```

---

## 6. 更新 TODO

```bash
nano docs/TODO.md
```

把完成的任务改成：

```markdown
- [x] 已完成事项
```

---

## 7. 提交 feature 分支

```bash
cd ~/tspi/rk3566_vision_terminal

git status
git add services docs patches scripts README.md
git commit -m "feat(module): complete feature stage with sdk patches"
git push
```

如果是第一次推送该 feature 分支：

```bash
git push -u origin feature/xxx
```

---

## 8. 合并回 main

```bash
git switch main
git pull
git merge feature/xxx
git push
```

如果出现冲突，先不要乱操作，执行：

```bash
git status
```

查看冲突文件，解决后再：

```bash
git add .
git commit
git push
```

---

## 9. 删除已完成分支

```bash
git branch -d feature/xxx
git push origin --delete feature/xxx
git fetch --all --prune
```

查看确认：

```bash
git branch -a
```

---

## 10. 打 tag

如果这是一个明确阶段版本，打 tag。

例如完成 `v0.2.0`：

```bash
git tag -a v0.2.0 -m "support ota_slotctl active slot detection"
git push origin v0.2.0
```

查看 tag：

```bash
git tag
```

查看 tag 内容：

```bash
git show v0.2.0
```

---

# 六、阶段完成后的检查表

阶段完成后，建议执行：

```bash
cd ~/tspi/rk3566_vision_terminal

git status
git log --oneline --decorate --graph --all -20
ls -lh patches/sdk
find patches/sdk -name "*.patch" -size 0 -print
grep -RIn "defconfig.bak\|backup\|Binary files\|ota_slotctl" patches/sdk
git tag
```

确认：

```text
main 分支干净
远程 main 已同步
功能分支已删除
patch 没有空文件
patch 没有脏内容
DEVLOG 已更新
SDK_CHANGELOG 已更新
TODO 已更新
如有必要已打 tag
```

---

# 七、patch 回退时执行的命令

如果某个阶段发现 patch 有问题，需要回退。

## 1. 进入对应 SDK 子仓库

例如回退 Buildroot patch：

```bash
cd ~/tspi/linux/buildroot
```

## 2. 检查能否反向回退

```bash
git apply -R --check ~/tspi/rk3566_vision_terminal/patches/sdk/xxxx.patch
```

## 3. 正式回退

```bash
git apply -R ~/tspi/rk3566_vision_terminal/patches/sdk/xxxx.patch
```

## 4. 查看状态

```bash
git status --short
git diff --stat
```

## 5. 回项目仓库记录

```bash
cd ~/tspi/rk3566_vision_terminal

nano docs/SDK_CHANGELOG.md
nano docs/DEVLOG.md

git add docs/SDK_CHANGELOG.md docs/DEVLOG.md
git commit -m "docs: record sdk patch rollback"
git push
```

---

# 八、常用命令速查

## 项目仓库状态

```bash
cd ~/tspi/rk3566_vision_terminal

git status
git branch -vv
git log --oneline --decorate -5
```

## 所有分支

```bash
git branch -a
```

## 分支图

```bash
git log --oneline --decorate --graph --all -20
```

## 更新远程信息

```bash
git fetch --all --prune
```

## 创建功能分支

```bash
git switch main
git pull
git switch -c feature/xxx
```

## 提交当前进度

```bash
git add .
git commit -m "wip: update development progress"
git push
```

## 合并分支

```bash
git switch main
git pull
git merge feature/xxx
git push
```

## 删除分支

```bash
git branch -d feature/xxx
git push origin --delete feature/xxx
git fetch --all --prune
```

## 打 tag

```bash
git tag -a v0.x.0 -m "version description"
git push origin v0.x.0
```

---

# 九、推荐执行频率总表

| 事项 | 每天 | 每周 | 阶段完成 | 说明 |
|---|---|---|---|---|
| `git status` | 必须 | 必须 | 必须 | 查看工作区状态 |
| `git pull` | 建议 | 必须 | 必须 | 同步远程 |
| `git push` | 必须 | 必须 | 必须 | 保存远程备份 |
| 更新 `DEVLOG.md` | 建议 | 必须 | 必须 | 记录开发进度 |
| 更新 `TODO.md` | 可选 | 建议 | 必须 | 记录任务完成情况 |
| 跑 `check_sdk_status.sh` | 改 SDK 时必须 | 必须 | 必须 | 检查 SDK 修改 |
| 生成 SDK patch | 不建议每天 | 需要时 | 必须 | 阶段稳定后归档 |
| 更新 `SDK_CHANGELOG.md` | 有 SDK 修改时 | 建议 | 必须 | 记录 SDK 修改原因 |
| 删除已合并分支 | 不一定 | 建议 | 必须 | 保持分支清爽 |
| 打 tag | 否 | 否 | 建议 | 记录阶段版本 |

---

# 十、核心口诀

```text
开工先 pull，收工先 push。
每天记 DEVLOG。
改 SDK 跑脚本。
临时实验不 patch。
阶段稳定再 patch。
main 里的 patch 不覆盖。
feature 里的 patch 可覆盖。
坏 patch 用 -R 回退。
阶段成果打 tag。
功能合并后删分支。
```
