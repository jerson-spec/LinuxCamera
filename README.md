# LinuxCamera

基于 i.MX6ULL + Linux V4L2 的相机项目。

> **当前状态：W3 完成（预览优化 + 多分辨率 + letterbox）**
>
> 已实现：
> - V4L2 MMAP 采集、格式/分辨率枚举、MJPEG 保存 JPEG
> - ini 配置、分级日志、CMake 交叉编译、gdbserver 调试
> - LCD framebuffer 打开、mmap、16bpp/32bpp 画像素
> - YUYV → RGB（FULL / LIMIT）
> - 合成 YUYV 色条上屏
> - 摄像头 YUYV 单帧 → RGB → LCD（v0.1）
> - 连续预览 + 双缓冲（FBIOPAN_DISPLAY）+ 每秒 FPS/耗时/丢帧/内存统计（v0.5）
> - **定点转换 + 按行写 framebuffer：CPU 忙占比 98% → 34%（W3）**
> - **640×480@30fps、letterbox 居中（W3）**
>
> 尚未实现：
> - 缩放（放大/缩小）
> - 触摸拍照、JPEG 存卡、OSD
> - 网络推流
> - 8 小时稳定性
>
> **当前不是 v1.0。**

---

## 目录

- [功能](#功能)
- [当前验证结果](#当前验证结果)
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
| YUYV → RGB（FULL/LIMIT） | ✅ 已验证 |
| RGB565（16bpp）/ XRGB8888（32bpp）写像素 | ✅ 已验证 |
| 摄像头 YUYV 单帧 → LCD | ✅ v0.1 已验证 |
| 连续预览 + 双缓冲（`FBIOPAN_DISPLAY`） | ✅ v0.5 已验证 |
| FPS / 耗时 / 丢帧 / VmRSS 统计 | ✅ v0.5 已验证 |
| 多分辨率（800×600@15 / 640×480@30） | ✅ 已验证 |
| letterbox 居中显示 | ✅ 已验证 |
| 缩放（放大/缩小） | ❌ 未实现 |
| 触摸/按键拍照 | ❌ 未实现 |
| OSD | ❌ 未实现 |
| 网络推流 | ❌ 未实现 |

---

## 当前验证结果

当前板子：

- 板子：100ask i.MX6ULL
- 摄像头：`/dev/video1`
- 摄像头实际输出：**800×600 YUYV**
  - 配置里请求 `1024×600`，驱动调整为 `800×600`
  - `bytesperline = 1600 = 800 × 2`
- LCD：`/dev/fb0`，**1024×600**
- 显示结果：画面居中，800×600 时左右各 112 像素黑边；640×480 时四周都有黑边
- 这个黑边是**分辨率/宽高比不匹配**，不是 bug，用 letterbox 居中处理

已验证（v0.1）：

- YUYV 色条 FULL / LIMIT 均正常；
- 摄像头 YUYV 单帧可显示到 LCD；
- 程序能正常退出，无段错误。

已验证（v0.5，连续 10 分钟，2026-11-08）：

| 指标 | 实测 |
|---|---|
| 时长 / 帧数 | 604 s / 9036 帧 |
| 平均 fps | 14.96（稳态 15.0） |
| wait（poll + DQBUF） | 0.7 ms |
| conv（YUYV → RGB888） | 35.1 ms |
| draw（480k 次 `lcd_put_pixel`） | 30.6 ms |
| dropped | 0 |
| VmRSS | 6324 kB，全程不变 |
| 收尾 | DQbuf = Qbuf = Frames = 9036 |

结论：15 fps 是摄像头在 800×600 YUYV 下的上限（`v4l2-ctl --list-formats-ext`
只给出 15/10/5 三档），不是代码限制；当时程序约 66 ms/帧，CPU 忙占比约 98%。

已验证（W3 优化，2026-11-09）：

- YUYV→RGB 定点化 + 提取色度项 + 内联 clamp：conv 35.1 → **14.0 ms**；
- 按行写 framebuffer：draw 30.6 → **8.6 ms**；
- 800×600 仍是 15 fps，但 CPU 忙占比从 98% 降到 **34%**；
- 640×480@30 跑通：`fps 30.0, wait 18.2, conv 9.4, draw 5.5, dropped 0`；
- letterbox 居中显示已生效；
- 暗光下摄像头会自动降帧（30→25），不是丢帧，详见 `docs/performance.md`。

---

## 数据流

### 采集/保存

```text
/dev/video1
    │
    ▼
V4L2 MMAP 采集
    │
    ├── MJPEG → 保存 .jpg
    │
    └── YUYV → 下一阶段连续预览
```

### 摄像头单帧预览（v0.1）

```text
DQBUF
  │
  ▼
mem[buf.index]（YUYV，bytesperline = width*2）
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

### 连续预览（v0.5）

```text
while (!g_stop)
    poll(2000ms)
      → DQBUF
      → yuyv_to_rgb()            [conv]
      → QBUF（尽早归还，驱动继续采）
      → lcd_put_pixel() × 480k   [draw]
      → lcd_flip()               FBIOPAN_DISPLAY 切前后台
      → 每秒统计 fps / wait / conv / draw / dropped / VmRSS
```

双缓冲要点：

- `FBIOPUT_VSCREENINFO` 把 `yres_virtual` 设为 `2 × yres`；
- 一屏 1024×600×4 B（32bpp，`line_length = 4096`），两块共约 4.9 MB；
- 只画后台，画完 `FBIOPAN_DISPLAY` 切到前台；
- pan 失败会自动降级为单缓冲并继续运行。

---

## 目录结构

```text
LinuxCamera/
├── CMakeLists.txt
├── README.md
├── LICENSE.txt
├── .gitignore
├── cmake/
│   └── toolchain-arm.cmake
├── config/
│   ├── camera.ini
│   ├── lcd.ini
│   └── trans.ini
├── include/
│   ├── config.h
│   ├── log.h
│   ├── lcd.h
│   └── yuyv.h
├── src/
│   ├── config.c
│   ├── log.c
│   ├── lcd.c
│   ├── yuyv.c
│   └── main.c
├── tests/
│   ├── manual/
│   │   ├── test_Get_images.c
│   │   ├── test_Draw_region.c
│   │   ├── test_Trans_YUYV.c
│   │   └── test_camera_preview.c
│   └── unit/
│       └── config_test.c
├── third_party/
│   └── inih/
├── docs/
│   ├── architecture.md
│   ├── board.md
│   ├── roadmap.md
│   ├── handoff.md
│   ├── handoff-prompt.md
│   ├── debug-log.md
│   └── performance.md
├── scripts/
│   ├── build.sh
│   ├── build_dbg.sh
│   ├── deploy.sh
│   ├── deploy_dbg.sh
│   └── gdb.sh
├── build-arm/
└── stage/
```

说明：

- `src/`：生产代码，不带 `main`；
- `tests/manual/`：需要板子/摄像头/LCD 的手动测试；
- `tests/unit/`：无硬件依赖的自动测试；
- `src/main.c`：当前仍是配置检查占位程序，不是正式 camera 主程序；
- `build-arm/`、`stage/`：生成物，未提交；

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

这是我服务器上的 SDK 路径。

### 2. 本机构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
```

### 3. 交叉编译

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-arm -j"$(nproc)"
```

Debug 版：

```bash
cmake -S . -B build-arm-dbg \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build-arm-dbg -j"$(nproc)"
```

检查：

```bash
file build-arm/test_camera_preview
file build-arm-dbg/test_camera_preview
```

### 4. 构建单个目标

```bash
cmake --build build-arm --target test_camera_preview -j"$(nproc)"
cmake --build build-arm --target test_Get_images -j"$(nproc)"
cmake --build build-arm --target test_Trans_YUYV -j"$(nproc)"
```

### 5. 安装到 staging（可选）

```bash
cmake --install build-arm --prefix "$PWD/stage"
```

当前 install 只安装 `camera` 和配置；测试程序需要手动推送。

---

## 部署到板子

使用 ADB，板子路径以 `/root/LinuxCamera` 为例。

### 1. 确认连接

```bash
adb devices
```

### 2. 创建目录

```bash
adb shell mkdir -p /root/LinuxCamera/config
```

### 3. 推送程序和配置

```bash
adb push build-arm/test_Get_images /root/LinuxCamera/
adb push build-arm/test_Draw_region /root/LinuxCamera/
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/
adb push build-arm/test_camera_preview /root/LinuxCamera/

adb push config/camera.ini /root/LinuxCamera/config/
adb push config/lcd.ini /root/LinuxCamera/config/
adb push config/trans.ini /root/LinuxCamera/config/
```

### 4. 板子上执行

```bash
adb shell
cd /root/LinuxCamera
chmod +x test_Get_images test_Draw_region test_Trans_YUYV test_camera_preview
```

---

## 配置文件

### `config/camera.ini`

```ini
[camera]
device=/dev/video1
width=1024
height=600
format=YUYV
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
| `width/height` | **请求**分辨率；驱动可能调整 |
| `format` | `MJPG` / `MJPEG` / `YUYV` |
| `buffers` | V4L2 buffer 数，2–8 |
| `flag` | `1` 只枚举；`0` 采集（`test_Get_images` 使用） |
| `save_count` | 保存张数，必须 > 0 |
| `save_dir` | 保存目录 |
| `log_level` | DEBUG/INFO/WARN/ERROR |
| `log_file` | 日志文件 |

注意：

- 当前摄像头请求 `1024×600`，实际返回 `800×600`；
- 代码会在日志/终端提示实际分辨率；
- `test_camera_preview` 不使用 `flag`；

### `config/lcd.ini`

```ini
[lcd]
device=/dev/fb0
color=0xffff0000
display_size=2
display_time=300

[lcd_log]
log_level=DEBUG
log_file=./lcd.log
```

| Key | 含义 |
|---|---|
| `device` | framebuffer 节点 |
| `color` | LCD 区域测试颜色 |
| `display_size` | 区域测试尺寸 |
| `display_time` | 区域测试/色条测试的保持时间（秒）；预览已不用 |
| `lcd_log` | LCD 日志配置 |

`test_Draw_region` 用它决定画面保持多久；`test_camera_preview` 从 v0.5 起改为连续预览，不再使用。

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
cd /root/LinuxCamera
./camera config/camera.ini
./config_test config/camera.ini
```

### 2. 采集/保存 JPEG

`camera.ini` 设置：

```ini
format=MJPG
flag=1
```

先枚举：

```bash
./test_Get_images
```

再设置：

```ini
flag=0
save_count=20
```

```bash
./test_Get_images
```

### 3. LCD 区域测试

```bash
./test_Draw_region
```

### 4. YUYV 合成色条

```bash
./test_Trans_YUYV
```

应显示：

```text
白 | 黄 | 青 | 绿 | 品红 | 红 | 蓝 | 黑
```

### 5. 摄像头连续预览

`camera.ini`（默认）：

```ini
format=YUYV
width=1024
height=600
```

运行：

```bash
./test_camera_preview
```

预期：

- 摄像头实际输出 800×600（请求 1024×600，驱动协商成 800×600）；
- 画面在 LCD 上居中，左右各 112 像素黑边；
- 每秒打印一行 `fps / wait / conv / draw / dropped / rss`；
- 按 Ctrl-C 退出，收尾打印 `DQbuf = Qbuf = Frames`。

想看 30fps，把 `width`/`height` 改成 640/480 再跑：摄像头在这个尺寸下支持 30fps，
程序会用 `VIDIOC_S_PARM` 主动请求。暗光下自动曝光会自动降帧（30→25…），属于正常现象。

---

## 日志

`test_Get_images` 使用日志：

```text
[2026-11-05 14:30:12.123] [INFO] test_Get_images.c:...: ...
```

级别：

```text
DEBUG < INFO < WARN < ERROR
```

输出：

- 终端 `stderr`
- `log_file` 指定文件

查看：

```bash
tail -f camera.log
```

注意：

- 当前没有日志轮转；
- 当前是单线程写文件；
- 日志路径相对于运行目录；
- `lcd_init()` 会重新初始化日志到 `lcd.log`，`lcd_deinit()` 会关闭日志。

---

## GDB 调试

### 1. 板子上启动 gdbserver

```bash
cd /root/LinuxCamera
gdbserver :1234 ./test_camera_preview
```

### 2. Ubuntu 上 ADB 端口转发

```bash
adb forward tcp:1234 tcp:1234
```

### 3. Ubuntu 上启动交叉 gdb

```bash
GDB=$(find "$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot" \
      -type f -name '*-gdb' | head -n 1)

"$GDB" build-arm-dbg/test_camera_preview
```

gdb 里：

```gdb
set pagination off
set confirm off
target remote 127.0.0.1:1234
b main
c
```

常用断点：

```gdb
b test_camera_preview.c:176     # S_FMT
b test_camera_preview.c:294     # DQBUF
b yuyv_to_rgb
b lcd_put_pixel
```

查看变量：

```gdb
p camera_cfg
p vfmt.fmt.pix
p buf.bytesused
p mem_addr
p lcd_var_screeninfo_t
```

注意：

- 远程调试不使用 `r`，用 `c`；
- 程序退出后 gdbserver 结束；
- 当前板子没有 IP，通过 ADB 转发。

---

## 已知限制

1. 摄像头实际输出 800×600，LCD 1024×600，居中后左右各 112 像素黑边，正常；
2. 800×600 下摄像头只有 15 fps；640×480 可以到 30 fps；
3. 暗光下自动曝光会主动降帧（30→25→20…），不是丢帧；
4. `lcd_write_line_rgb888()` 只支持 32bpp，其他 bpp 会直接返回失败；
5. `yuyv_to_rgb()` 没有 `src_stride` 参数，当前仅支持 `bytesperline == width*2`；
6. `yuyv.c` 使用全局 `full` 状态，依赖先调用 `Is_full_range()`；
7. `app_config_t` 同时包含 camera/lcd/log/trans，偏大；
8. `lcd_init()` 会把日志重定向到 `lcd.log`，`lcd_deinit()` 会关闭日志，所以 `camera.log` 基本是空的；
9. 首帧延迟约 1–1.6 s（摄像头启动 + 曝光收敛）；
10. 没有触摸/OSD/网络；没有单元测试/CI；

---

## Roadmap

### v0.1

- [x] V4L2 YUYV 采集
- [x] YUYV → RGB
- [x] LCD 单帧显示
- [x] 实际 800×600，左上角 1:1 显示
- [x] tag `v0.1`

### v0.5

- [x] 连续预览
- [x] 双缓冲 + `FBIOPAN_DISPLAY`
- [x] FPS / 耗时 / 丢帧 / VmRSS 统计
- [x] 10 分钟不崩（604 s，9036 帧，0 丢帧）

### W3 优化与适配（当前，2026-11-09）

- [x] YUYV→RGB 定点化（conv 35.1 → 14.0 ms）
- [x] 按行写 framebuffer（draw 30.6 → 8.6 ms）
- [x] CPU 忙占比 98% → 34%
- [x] 640×480@30 验证
- [x] letterbox 居中
- [ ] 里程碑标签

### v1.0

- [x] 多分辨率（配置驱动）
- [x] letterbox
- [ ] 触摸/按键拍照
- [ ] JPEG 存卡
- [ ] OSD
- [ ] 8 小时稳定

### 后续

- [ ] MJPEG over HTTP
- [ ] 分层重构
- [ ] 单元测试
- [ ] Buildroot 精简镜像
- [ ] 开机自启
- [ ] 看门狗

---

## 第三方组件与 License

- 本项目自身：**MIT License**，见根目录 `LICENSE.txt`。
- [inih](https://github.com/benhoyt/inih)：BSD-3-Clause，见 `third_party/inih/LICENSE.txt`。
- 100ask i.MX6ULL SDK / 开发板资料：遵循其原始许可。

---

## 致谢

- inih：Ben Hoyt 及贡献者
- 100ask i.MX6ULL 开发板与 SDK
- Linux V4L2 / framebuffer 社区文档
