# SDK 修改记录

本文件用于记录 RK3566 Vision Terminal 项目中所有 SDK 层面的修改。

厂商 SDK 本体不上传 GitHub，只记录以下内容：

- 为什么修改
- 修改了哪些 SDK 文件
- 对应的 patch 或配置快照
- 编译命令
- 板端验证命令
- 验证结果

---

## 修改记录模板

### 日期 - 修改标题

#### 一、修改目的

说明为什么需要这次 SDK 修改。

#### 二、修改的 SDK 文件

```text
/path/in/sdk/file1
/path/in/sdk/file2
```
#### 三、对应的项目仓库文件
patches/sdk/xxxx.patch
configs/sdk_snapshot/xxxx
scripts/xxxx.sh
#### 四、编译命令
cd /work/linux
./build.sh all
./mkfirmware.sh
./build.sh updateimg
#### 五、板端验证命令

#### 六、验证结果

说明这次修改是否生效，是否还有遗留问题。