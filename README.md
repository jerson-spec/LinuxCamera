# LinuxCamera

基于 i.MX6ULL + Linux V4L2 的相机项目。

> **当前状态：LCD / YUYV Milestone（pre-v0.1）**
>
> 已实现：
> - V4L2 MMAP 采集、格式/分辨率枚举、JPEG 保存
> - ini 配置、分级日志、CMake 交叉编译、gdbserver 调试
> - LCD framebuffer 打开、mmap、画像素
> - YUYV → RGB 转换（FULL / LIMIT）
> - 合成 YUYV 色条上屏
>
> 尚未实现：
> - 摄像头 YUYV 帧直接显示到 LCD
> - 连续预览、双缓冲、letterbox
> - 触摸拍照、OSD、网络推流
>
> **当前还不是完整 v0.1。**

---

## 目录

- [功能](#功能)
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
| 格式/分辨率枚举 | ✅ 基本可用 |
| MJPEG 保存 JPEG | ✅ 已验证 |
| ini 配置 | ✅ 已实现 |
| 分级日志 | ✅ 已实现 |
| CMake + Buildroot SDK 交叉编译 | ✅ 已实现 |
| gdbserver + ADB 端口转发 | ✅ 基本流程已验证 |
| LCD framebuffer 打开、mmap、画像素 | ✅ 已实现 |
| YUYV → RGB（FULL/LIMIT） | ✅ 合成色条已验证 |
| RGB565 写入（16bpp） | ✅ 已验证 |
| 摄像头 YUYV → LCD | ❌ 下一步 |
| 连续预览 / 双缓冲 / FPS | ❌ 未实现 |
| letterbox / 缩放 | ❌ 未实现 |
| 触摸/按键拍照 | ❌ 未实现 |
| OSD | ❌ 未实现 |
| 网络推流 | ❌ 未实现 |

当前验证过的场景：

- 板子：i.MX6ULL
- 摄像头：`/dev/video1`
- 分辨率：640×480
- 格式：MJPEG（采集/保存）
- LCD：`/dev/fb0`
- YUYV 色条：FULL/LIMIT 均能显示
- 传输：ADB

---

## 数据流

当前已实现：

```text
Camera (/dev/video1)
        │
        ▼
   V4L2 MMAP 采集
        │
        ├── MJPEG ──▶ 保存 JPEG
        │
        └── （下一步）YUYV ──▶ RGB ──▶ LCD
```

LCD 测试路径：

```text
generate_yuyv_region()
        │
        ▼
   yuyv_to_rgb()
        │
        ▼
   lcd_put_pixel()
        │
        ▼
   /dev/fb0
```

---

## 目录结构

```text
LinuxCamera/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── cmake/
│   └── toolchain-arm.cmake
├── config/
│   ├── camera.ini
│   ├── lcd.ini
│   └── trans.ini
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
├── LCD/
│   ├── test_Draw_region.c
│   └── include/
│       └── test_Draw_region.h
├── Trans/
│   └── test_Trans_YUYV.c
├── build-arm/        # CMake 生成，未提交
└── stage/            # install 生成，未提交
```

说明：

- `src/main.c`：当前是配置检查占位程序；
- `V4L2/test_Get_images.c`：采集/保存测试；
- `LCD/test_Draw_region.c`：LCD 函数库（当前不单独生成可执行文件）；
- `Trans/test_Trans_YUYV.c`：当前 LCD + YUYV 色条演示程序；
- `tests/config_test.c`：配置模块测试；
- `build-arm/`、`stage/`：生成物，未提交。

后续会把带 `main` 的手动测试集中到 `tests/manual/`，把 LCD/YUYV 生产代码抽到 `src/`，当前结构先保留以保证可运行。

---

## 环境要求

### 主机

- Ubuntu 18.04/20.04 或更新版本
- CMake 3.16+
- GCC / GNU Make
- 100ask i.MX6ULL Buildroot SDK
- `adb`

本项目面向 Linux/V4L2，不支持 Windows MSVC 原生编译。

### 板子

- i.MX6ULL
- V4L2 摄像头节点，例如 `/dev/video1`
- framebuffer 节点，例如 `/dev/fb0`
- `gdbserver`（调试用）

Ubuntu 依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake linux-libc-dev adb
```

检查设备：

```bash
ls -l /dev/video*
ls -l /dev/fb*
v4l2-ctl -d /dev/video1 --list-formats-ext
fbset -i
```

---

## 构建

### 1. 修改工具链路径

编辑 `cmake/toolchain-arm.cmake`：

```cmake
set(SDK_ROOT "/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot")
```
这是我板子上的工具链目录。

### 2. 本机构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### 3. 交叉编译 Release/Debug

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-arm -j
```

Debug 版：

```bash
cmake -S . -B build-arm-dbg \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-arm.cmake \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build-arm-dbg -j
```

检查架构：

```bash
file build-arm/test_Get_images
file build-arm/test_Trans_YUYV
```

应看到 ARM 32-bit。

### 4. 构建单个目标

```bash
cmake --build build-arm --target test_Trans_YUYV -j
cmake --build build-arm --target test_Get_images -j
```

### 5. 安装到 staging（可选）

```bash
cmake --install build-arm --prefix "$PWD/stage"
```

当前 install 会安装：

```text
stage/bin/camera
stage/etc/camera/camera.ini
stage/etc/lcd/lcd.ini
stage/etc/trans/trans.ini
```

注意：当前测试程序默认读取项目内的 `config/`，安装后的 `/etc/...` 路径尚未接入。

---

## 部署到板子

使用 ADB，板子路径以 `~/LinuxCamera` 为例。

### 1. 确认连接

```bash
adb devices
```

### 2. 创建目录

```bash
adb shell mkdir -p ~/LinuxCamera/config
```

### 3. 推送程序和配置

```bash
adb push build-arm/test_Get_images ~/LinuxCamera/
adb push build-arm/test_Trans_YUYV ~/LinuxCamera/

adb push config/camera.ini ~/LinuxCamera/config/
adb push config/lcd.ini ~/LinuxCamera/config/
adb push config/trans.ini ~/LinuxCamera/config/
```

### 4. 板子上运行

```bash
adb shell
cd ~/LinuxCamera
chmod +x test_Get_images test_Trans_YUYV
```

---

## 配置文件

### `config/camera.ini`

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

| Key | 含义 |
|---|---|
| `device` | 摄像头节点 |
| `width/height` | 采集分辨率 |
| `format` | `MJPG` / `MJPEG` / `YUYV` |
| `buffers` | V4L2 buffer 数，2–8 |
| `flag` | `1` 只枚举；`0` 采集 |
| `save_count` | 保存张数，必须 > 0 |
| `save_dir` | 保存目录 |
| `log_level` | DEBUG/INFO/WARN/ERROR |
| `log_file` | 日志文件 |

### `config/lcd.ini`

```ini
[lcd]
device=/dev/fb0
color=0xffff0000
display_size=2
display_time=10

[lcd_log]
log_level=DEBUG
log_file=./lcd.log
```

当前 `test_Trans_YUYV` 只读取 `device`；`color/display_size/display_time` 是 LCD 区域测试保留字段。

### `config/trans.ini`

```ini
[trans]
range=FULL
trans_display_time=100
```

| Key | 含义 |
|---|---|
| `range` | `FULL` 或 `LIMIT` |
| `trans_display_time` | 色条显示时间（秒） |

---

## 运行

### 1. 配置检查

```bash
cd ~/LinuxCamera
./camera config/camera.ini
./config_test config/camera.ini
```

### 2. 采集/保存 JPEG

`camera.ini`：

```ini
[camera]
...
flag=1
```

先枚举格式/分辨率：

```bash
./test_Get_images
```

根据输出修改 `format`、`width`、`height`，然后：

```ini
flag=0
save_count=20
```

运行：

```bash
./test_Get_images
```

输出：

```text
Taking picture: 0
...
```

图片在 `./images/`。

### 3. LCD + YUYV 色条

从项目目录运行（当前会读取 `config/lcd.ini`）：

```bash
./test_Trans_YUYV
```

显示 8 条色条：

```text
白 | 黄 | 青 | 绿 | 品红 | 红 | 蓝 | 黑
```

`config/trans.ini` 的 `range` 改为 `LIMIT` 可测试有限范围。

程序会按 `trans_display_time` 秒后退出；

---

## 日志

`test_Get_images` 使用日志：

```text
[2026-11-03 14:30:12.123] [INFO] test_Get_images.c:...: ...
```

级别：

```text
DEBUG < INFO < WARN < ERROR
```

日志同时输出到：

- 终端 `stderr`
- `log_file` 指定的文件

查看：

```bash
tail -f camera.log
```

注意：

- 当前没有日志轮转；
- 当前是单线程写文件；
- 日志路径相对于运行目录。

---

## GDB 调试

### 1. 板子上启动 gdbserver

```bash
cd ~/LinuxCamera
gdbserver :1234 ./test_Trans_YUYV
```

### 2. Ubuntu 上 ADB 端口转发

```bash
adb forward tcp:1234 tcp:1234
```

### 3. Ubuntu 上启动交叉 gdb

```bash
GDB=$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/host/bin/arm-buildroot-linux-gnueabihf-gdb

"$GDB" build-arm-dbg/test_Trans_YUYV
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
b Get_lcd_parameters
b yuyv_to_rgb
b lcd_put_pixel
b generate_yuyv_region
```

查看变量：

```gdb
p mem_addr
p lcd_var_screeninfo_t
p cfg
p w
p h
```

注意：

- 远程调试不使用 `r`，用 `c`；
- `test_Trans_YUYV` 跑完会退出，gdbserver 也会结束；
- 如果使用网线，把 `127.0.0.1:1234` 换成板子 IP。

---

## 已知限制

1. 摄像头 YUYV 帧还没有直接显示到 LCD；
2. `yuyv_to_rgb()` 目前假设源数据无 stride，摄像头 `bytesperline` 可能不同；
3. `lcd_put_pixel()` 逐像素调用，适合单帧测试，不适合高帧率预览；
4. `Get_lcd_parameters()` 硬编码 `config/lcd.ini`；
5. `test_Draw_region` 当前是库，不是独立可执行测试；
6. `app_config_t` 同时包含 camera/lcd/trans/log，后续需要拆分；
7. `test_Get_images` 仍把 YUYV 保存为 `.jpg`，后续需修后缀；
8. 没有双缓冲、letterbox、触摸、OSD、网络；
9. 没有连续 8 小时稳定性验证；
10. 没有单元测试/CI。

---

## Roadmap

### 已完成

- [x] V4L2 MMAP 采集
- [x] 格式/分辨率枚举
- [x] MJPEG 保存 20 张
- [x] ini 配置
- [x] 分级日志
- [x] CMake 交叉编译
- [x] gdbserver + ADB 调试
- [x] LCD framebuffer + 画像素
- [x] YUYV → RGB 转换（FULL/LIMIT）
- [x] 合成 YUYV 色条上屏

### 下一步：v0.1

- [ ] 摄像头 YUYV 帧直接显示到 LCD
- [ ] 处理 `bytesperline` / stride
- [ ] 处理摄像头分辨率与 LCD 分辨率不匹配
- [ ] 单帧预览稳定

### v0.5

- [ ] 连续预览
- [ ] 双缓冲 + `FBIOPAN_DISPLAY`
- [ ] FPS 统计
- [ ] 连续 10 分钟不崩

### v1.0

- [ ] 多分辨率
- [ ] letterbox
- [ ] 触摸/按键拍照
- [ ] JPEG 存卡
- [ ] OSD
- [ ] 连续 8 小时稳定

---

## 第三方组件与 License

- [inih](https://github.com/benhoyt/inih)：BSD-3-Clause
- 100ask i.MX6ULL SDK / 开发板资料：遵循其原始许可。

---

## 致谢

- inih：Ben Hoyt 及贡献者
- 100ask i.MX6ULL 开发板与 SDK
- Linux V4L2 / framebuffer 社区文档