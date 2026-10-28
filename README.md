# LinuxCamera

基于 i.MX6ULL + Linux V4L2 的相机项目。

> **当前状态：Capture Milestone 1（pre-display）**
>
> 已实现 V4L2 采集、格式/分辨率枚举、ini 配置、分级日志和 JPEG 保存。
>
> **尚未实现 LCD 显示、YUYV→RGB565、触摸、OSD。**

---

## 目录

- [功能](#功能)
- [当前不包含的功能](#当前不包含的功能)
- [数据流](#数据流)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [构建](#构建)
- [部署到板子](#部署到板子)
- [配置文件](#配置文件)
- [运行](#运行)
- [日志](#日志)
- [GDB 调试](#gdb-调试)
- [已知限制](#已知限制)
- [Roadmap](#roadmap)
- [第三方组件与 License](#第三方组件与-license)

---

## 功能

| 功能 | 状态 |
|---|---|
| V4L2 MMAP 采集 | ✅ 已实现 |
| 4 个 V4L2 buffer（可配置 2–8） | ✅ 已实现 |
| `QUERYCAP / S_FMT / REQBUFS / QUERYBUF / mmap / QBUF / STREAMON / DQBUF / QBUF / STREAMOFF` | ✅ 已实现 |
| 枚举设备支持的格式 | ✅ 基本可用 |
| 枚举每种格式支持的分辨率 | ✅ 基本可用 |
| ini 配置文件 | ✅ 已实现 |
| 分级日志 DEBUG/INFO/WARN/ERROR | ✅ 已实现 |
| 保存 JPEG 到指定目录 | ✅ 已实现（MJPEG 路径已验证） |
| CMake + Buildroot SDK 交叉编译 | ✅ 已实现 |
| gdbserver + ADB 端口转发调试 | ✅ 已验证基本流程 |
| LCD 显示 | ❌ 未实现 |
| YUYV→RGB565 | ❌ 未实现 |
| 双缓冲 / letterbox | ❌ 未实现 |
| 触摸/按键拍照 | ❌ 未实现 |
| OSD | ❌ 未实现 |
| 网络推流 | ❌ 未实现 |

当前实际验证过的场景：

- 板子：i.MX6ULL
- 摄像头设备：`/dev/video1`
- 格式：MJPEG
- 分辨率：640×480
- 保存数量：20 张
- 传输方式：ADB

不同摄像头支持的分辨率/格式不同，请先用 `flag=1` 枚举。

---

## 当前不包含的功能

这个项目目前只是**采集里程碑**，不是完整相机：

- 没有 LCD/framebuffer 显示；
- 没有 YUYV→RGB565 转换；
- 没有 letterbox；
- 没有触摸/按键交互；
- 没有 OSD；
- 没有连续 8 小时稳定性验证；
- 没有网络推流；
- 没有单元测试/CI。

`src/main.c` 目前只是配置检查占位程序，真正的采集程序是 `V4L2/test_Get_images.c`。后续会把采集逻辑重构到 `camera` 主程序。

---

## 数据流

当前版本：

```text
Camera (/dev/video1)
        │
        ▼
   V4L2 MMAP 采集
        │
        ▼
     DQBUF
        │
        ▼
  保存 JPEG（MJPEG）
```

后续目标：

```text
Camera
   │
   ▼
V4L2 采集
   │
   ├── YUYV ──▶ RGB565 ──▶ /dev/fb0
   │
   └── MJPEG ─▶ 拍照/存卡
```

---

## 目录结构

```text
LinuxCamera/
├── CMakeLists.txt
├── cmake/
│   └── toolchain-arm.cmake
├── config/
│   └── camera.ini
├── include/
│   ├── config.h
│   └── log.h
├── src/
│   ├── config.c
│   ├── log.c
│   └── main.c
├── tests/
│   └── config_test.c
├── third_party/
│   └── inih/
│       ├── LICENSE.txt
│       ├── ini.c
│       └── ini.h
├── V4L2/
│   └── test_Get_images.c
├── LCD/                     # 预留，当前为空
├── build-arm/               # CMake 生成，未提交
└── stage/                   # cmake --install 生成，未提交
```

说明：

- `src/main.c`：当前是配置检查程序，不是完整 camera 应用；
- `V4L2/test_Get_images.c`：当前真正的采集测试程序；
- `tests/config_test.c`：配置模块独立测试；
- `third_party/inih/`：第三方 ini 解析器，不要修改；
- `build-arm/`、`stage/`：本地生成物。

 `.gitignore`：

```gitignore
build*/
stage/
```

---

## 环境要求

### 主机

- Ubuntu 18.04/20.04 或更新版本；
- CMake 3.16+；
- GCC / GNU Make；
- 100ask i.MX6ULL Buildroot SDK；
- `adb`（用于传文件和 gdb 端口转发）。

> 本项目面向 Linux/V4L2，不支持 Windows MSVC 原生编译。

### 板子

- i.MX6ULL；
- 可用的 V4L2 摄像头节点，例如 `/dev/video1`；
- 板子上有 `gdbserver`（用于调试）；
- 板子与主机通过 ADB 或网络连接。

Ubuntu 上安装基础依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake linux-libc-dev adb
```

检查摄像头：

```bash
ls -l /dev/video*
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video1 --list-formats-ext
```

如果没有 `v4l2-ctl`：

```bash
sudo apt install v4l-utils
```

---

## 构建

### 1. 修改交叉工具链路径

`cmake/toolchain-arm.cmake` 里默认写的是：

```cmake
set(SDK_ROOT "/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot")
```

这是我服务器上的 SDK 路径。

### 2. 本机构建（用于 config/log 测试）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

产物：

```text
build/camera
build/config_test
build/test_Get_images
```

目标设备设备节点不是 `/dev/video1` 时，可以运行：

```bash
./build/config_test config/camera.ini
```
对应修改device为实际的设备节点。

### 3. 交叉编译 ARM 版本

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-arm -j

file build-arm/test_Get_images
```

应该看到：

```text
ELF 32-bit LSB executable, ARM, EABI5 ...
```

### 4. Debug 版本（用于 gdb）

```bash
cmake -S . -B build-arm-dbg \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm.cmake \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build-arm-dbg -j
```

`Debug` 版本带 `-g`，变量不会被优化掉，适合 gdb。

### 5. 安装到 staging（可选）

```bash
cmake --install build-arm --prefix "$PWD/stage"
```

结果：

```text
stage/
├── bin/
│   └── camera
└── etc/
    └── camera/
        └── camera.ini
```

注意：

- 当前 `install` 只安装 `camera` 和 `camera.ini`；
- `test_Get_images` 不包含在 install 中，需要手动 `adb push`；
- `camera` 目前还是配置检查占位程序。

---

## 部署到板子

本文使用 ADB。板子路径以 `~/LinuxCamera` 为例。

### 1. 确认 ADB 连接

```bash
adb devices
```

应该看到设备处于 `device` 状态。

### 2. 创建目录

```bash
adb shell mkdir -p ~/LinuxCamera/config
```

### 3. 推送程序和配置

```bash
adb push build-arm/test_Get_images ~/LinuxCamera/
adb push config/camera.ini ~/LinuxCamera/config/
```

如果使用 Debug 版本：

```bash
adb push build-arm-dbg/test_Get_images ~/LinuxCamera/test_Get_images_dbg
```

### 4. 板子上运行

```bash
adb shell
cd ~/LinuxCamera
chmod +x test_Get_images
./test_Get_images
```

也可以指定配置文件：

```bash
./test_Get_images /path/to/camera.ini
```

---

## 配置文件

默认路径：

```text
config/camera.ini
```

当前示例：

```ini
[camera]
device=/dev/video1
width=640
height=480
format=MJPG
buffers=4
flag=1

[storage]
save_count=20
save_dir=./images

[log]
log_level=INFO
log_file=./camera.log
```

### 配置项说明

#### `[camera]`

| Key | 含义 | 说明 |
|---|---|---|
| `device` | V4L2 设备节点 | 例如 `/dev/video1` |
| `width` | 采集宽度 | 必须是摄像头支持的分辨率 |
| `height` | 采集高度 | 必须是摄像头支持的分辨率 |
| `format` | 像素格式 | `MJPG`、`MJPEG`、`YUYV` |
| `buffers` | 请求的 V4L2 buffer 数 | 2–8，默认 4 |
| `flag` | 工作模式 | `1` 只枚举；`0` 采集 |

#### `[storage]`

| Key | 含义 | 说明 |
|---|---|---|
| `save_count` | 保存张数 | 必须大于 0；当前 `0` 会立即退出 |
| `save_dir` | 保存目录 | 相对当前工作目录，不存在会自动创建 |

#### `[log]`

| Key | 含义 | 说明 |
|---|---|---|
| `log_level` | 日志级别 | `DEBUG` / `INFO` / `WARN` / `ERROR` |
| `log_file` | 日志文件 | 相对当前工作目录，例如 `./camera.log` |

### 推荐工作流

1. 先设置 `flag=1`，运行程序，查看摄像头支持的格式和分辨率；
2. 根据输出修改 `format`、`width`、`height`；
3. 把 `flag` 改为 `0`；
4. 重新运行，开始采集并保存图片。

示例输出：

```text
index=0
format:MJPEG,1196444237 , support frame size: 640 x 480
format:MJPEG,1196444237 , support frame size: 1280 x 720
...
```

---

## 运行

### 枚举格式/分辨率

```ini
flag=1
```

板子上：

```bash
./test_Get_images
```

程序只枚举，不采集。

### 采集并保存

```ini
flag=0
save_count=20
save_dir=./images
```

板子上：

```bash
./test_Get_images
```

输出：

```text
Taking picture: 0
Taking picture: 1
...
```

图片保存为：

```text
images/image00.jpg
images/image01.jpg
...
```

查看日志：

```bash
cat camera.log
```

### 配置检查

`camera` 和 `config_test` 目前只打印配置：

```bash
./camera config/camera.ini
./config_test config/camera.ini
```

它们不采集。

---

## 日志

日志格式：

```text
[2026-10-28 14:30:12.123] [INFO] test_Get_images.c:276: Unable to start capture
```

级别：

```text
DEBUG < INFO < WARN < ERROR
```

`log_level=INFO` 时，`LOG_DEBUG` 不会输出；改成 `DEBUG` 才会显示。

日志同时输出到：

- 终端 `stderr`；
- `log_file` 指定的文件。

查看：

```bash
tail -f camera.log
```

注意：

- 日志文件需要可写；
- 当前没有日志轮转；
- 当前是单线程使用，后续多线程需要加锁；
- 日志文件路径相对于程序运行目录。

---

## GDB 调试

### 1. 板子上启动 gdbserver

```bash
cd ~/LinuxCamera
gdbserver :1234 ./test_Get_images
```

看到：

```text
Listening on port 1234
```

### 2. Ubuntu 上做 ADB 端口转发

```bash
adb devices
adb forward tcp:1234 tcp:1234
```

### 3. Ubuntu 上启动交叉 gdb

```bash
GDB=$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/host/bin/arm-buildroot-linux-gnueabihf-gdb

"$GDB" build-arm-dbg/test_Get_images
```

gdb 里：

```gdb
set pagination off
set confirm off
target remote 127.0.0.1:1234
b main
c
```

### 4. 常用断点

```gdb
b config_load
b test_Get_images.c:<行号>
c
```

查看变量：

```gdb
p cfg
p pixfmt
p vfmt
p buf
p/x buf.flags
p buf.bytesused
x/4bx mem[buf.index]
```

注意：

- 远程调试不使用 `r`，用 `c` 继续运行；
- 程序跑完 20 张后会退出，gdbserver 也会结束；
- 想重新调试，重新在板子上启动 `gdbserver`；
- 如果使用网线，可以把 `target remote 127.0.0.1:1234` 改成 `target remote <板子IP>:1234`。

清理转发：

```bash
adb forward --remove-all
```

如果板子没有 `gdbserver`，需要在 Buildroot 中打开：

```text
BR2_PACKAGE_GDB_SERVER=y
```

---

## 已知限制

当前是采集里程碑，不是最终版本：

1. 没有 LCD/framebuffer 显示；
2. 没有 YUYV→RGB565；
3. 没有 letterbox；
4. 没有触摸/按键/OSD；
5. 没有连续 8 小时稳定性验证；
6. 保存逻辑只验证了 MJPEG；`format=YUYV` 时仍按 `.jpg` 命名，文件实际不是 JPEG；
7. `poll` 没有超时，摄像头不出帧时可能永久阻塞；
8. `S_FMT` 后没有读回实际格式/分辨率；
9. `REQBUFS` 后没有检查实际 buffer 数量；
10. `DQBUF` 后没有检查 `V4L2_BUF_FLAG_ERROR` 和 `bytesused` 边界；
11. 输入源选择未实现，`VIDIOC_S_INPUT` 相关代码已禁用；
12. `save_count=0` 当前会立即退出，不是“不限制”；
13. 没有单元测试/CI；
14. 日志没有轮转，单线程写文件；
15. 当前 `camera` 可执行文件只是配置检查占位程序。

这些问题会在后续 v0.1–v1.0 中逐步修复。

---

## Roadmap

### Capture Milestone 1（当前）

- [x] V4L2 MMAP 采集
- [x] 4 buffer 循环
- [x] 格式/分辨率枚举
- [x] ini 配置
- [x] 分级日志
- [x] MJPEG 保存 20 张
- [x] CMake 交叉编译
- [x] gdbserver + ADB 基本调试

### v0.1 最小闭环

- [ ] 打开 `/dev/fb0`，获取 framebuffer 信息
- [ ] mmap framebuffer，画彩条
- [ ] 抓一帧 YUYV
- [ ] 实现 `yuyv_to_rgb565()`
- [ ] 按 `line_length` 写入 framebuffer
- [ ] 屏幕上出现画面

### v0.5 连续采集

- [ ] 连续采集
- [ ] 双缓冲 + `FBIOPAN_DISPLAY`
- [ ] FPS 统计
- [ ] 连续 10 分钟不崩

### v1.0 可用

- [ ] 多分辨率/多格式
- [ ] letterbox
- [ ] 触摸/按键拍照
- [ ] JPEG 编码与命名规则
- [ ] SD 卡容量检查
- [ ] OSD 显示 FPS/分辨率
- [ ] 连续 8 小时稳定

### 后续

- [ ] MJPEG over HTTP
- [ ] 分层重构
- [ ] 单元测试
- [ ] Buildroot 精简镜像
- [ ] 开机自启
- [ ] 看门狗

---

## 第三方组件与 License

- [inih](https://github.com/benhoyt/inih)：BSD-3-Clause，见 `third_party/inih/LICENSE.txt`。
- 100ask i.MX6ULL SDK / 开发板资料：遵循其原始许可。

---

## 致谢

- inih：Ben Hoyt 及贡献者
- 100ask i.MX6ULL 开发板与 SDK
- Linux V4L2 社区文档
