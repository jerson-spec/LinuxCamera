# 项目交接（Handoff）

> **新会话请先读本文。**
> 读完后再看：
> - `docs/architecture.md`
> - `docs/board.md`
> - `docs/roadmap.md`
> - `docs/debug-log.md`
> - `README.md`
>
> 本文目标是：让新的模型/新的你，在不看旧对话的情况下，快速知道
> **项目是什么、做到哪、怎么构建、怎么上板、下一步做什么、有哪些坑。**

---

## 0. 快速事实

| 项目 | 当前值 |
|---|---|
| 项目 | i.MX6ULL Linux 相机 |
| 当前阶段 | **pre-v0.1：LCD/YUYV 已分段跑通，摄像头帧尚未上屏** |
| 语言 | C |
| 采集 | V4L2 MMAP |
| 显示 | `/dev/fb0` framebuffer |
| 摄像头 | `/dev/video1`，MJPEG 已验证 |
| LCD | `/dev/fb0`，实际分辨率用 `fbset -i` 确认 |
| 工具链 | 100ask Buildroot SDK |
| 连接 | ADB，当前没有配置板子 IP |
| 当前标签 | `lcd-yuyv-milestone`（建议） |
| 当前不是 | v0.1、v0.5、v1.0 |

一句话：

> **采集、配置、日志、LCD、YUYV 转换都各自跑通了；下一步是把摄像头 YUYV 帧直接送到 LCD，完成 v0.1。**

---

## 1. 原始规划核心

原始规划文件：

```text
D:\Desktop\deepseek_harness_project\嵌入式Linux学习规划-相机主线.md
```

### 1.1 目标

从“只学过 API、没做过完整项目”走到：

> 一个能演示、能讲清楚、坏了能自己修的嵌入式相机项目。

主线：

```text
V4L2 采集 → YUYV/RGB 转换 → LCD 显示 → 拍照存卡 → 网络推流
```

### 1.2 12 周计划（摘要）

| 周 | 目标 |
|---|---|
| W0 | CMake 交叉编译、log、ini、gdbserver、Git |
| W1 | 一帧 YUYV → RGB565 → fb0 |
| W2 | 连续采集、FPS、超时 |
| W3 | 查表优化、letterbox、多分辨率 |
| W4 | 触摸/按键、OSD |
| W5 | libjpeg、文件命名、SD 卡 |
| W6 | MJPEG over HTTP |
| W7 | 分层重构、单元测试、设计文档 |
| W8–W9 | I2C/Input 驱动 |
| W10 | V4L2 深入（选学） |
| W11 | Buildroot、自启、看门狗 |
| W12 | 性能、README、演示、复盘 |

### 1.3 版本定义

- **v0.1**：抓 1 帧 YUYV → RGB565 → fb0，屏幕出现画面；
- **v0.5**：连续采集 + 双缓冲 + FPS，10 分钟不崩；
- **v1.0**：多分辨率/多格式 + letterbox + 触摸拍照 + JPEG 存卡 + OSD + 8 小时稳定；
- **v1.5**：MJPEG over HTTP；
- **v2.0**：分层重构 + 单元测试 + Buildroot + 开机自启 + 看门狗。

### 1.4 关键决策

- 第一个项目用 C，不用 C++/Qt；
- 6ULL 上预览用 YUYV，拍照用 MJPEG；
- 先 framebuffer，不直接上 DRM；
- 不做 Qt/LVGL/MQTT/RTSP，先把数据通路打通；
- 分辨率先 ≤ 800×480；
- 6ULL 无 GPU/VPU，后续可考虑 PXP；
- 先单摄像头；
- 核心数据通路自己写，CMake/ini/log 可以借鉴模板。

### 1.5 AI 协作原则

- 用户希望先自己写，AI 做 review，不直接代写核心数据通路；
- CMake/ini/log 属于工具类，可以给模板；
- 回答尽量结合当前代码和板子，不要只讲理论。

---

## 2. 当前仓库状态

### 2.1 目录

```text
LinuxCamera/
├── CMakeLists.txt
├── README.md
├── LICENSE.txt
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
│   │   └── test_camera_preview.c   # 空文件，下一步
│   └── unit/
│       └── config_test.c
├── third_party/
│   └── inih/
├── docs/
│   ├── architecture.md
│   ├── board.md
│   ├── roadmap.md
│   ├── handoff.md
│   ├── debug-log.md
│   └── performance.md              # 目前为空
├── scripts/
│   ├── build.sh
│   ├── build_dbg.sh
│   ├── deploy.sh
│   ├── deploy_dbg.sh
│   └── gdb.sh
├── build-arm/
└── stage/
```

### 2.2 已经能跑的功能

- V4L2 采集；
- 格式/分辨率枚举；
- MJPEG 保存 JPEG；
- ini 配置；
- 分级日志；
- CMake 交叉编译；
- gdbserver + ADB 转发；
- LCD framebuffer 打开/mmap/画像素；
- YUYV → RGB（FULL/LIMIT）；
- 合成 YUYV 色条上屏。

### 2.3 还没做的功能

- **摄像头 YUYV 帧直接显示到 LCD**（当前最重要）；
- 连续预览；
- 双缓冲；
- letterbox；
- 触摸/按键；
- OSD；
- libjpeg 拍照存卡；
- 网络推流；
- 8 小时稳定性；
- 单元测试/CI。

### 2.4 各测试程序

| 程序 | 状态 | 作用 |
|---|---|---|
| `test_Get_images` | ✅ 可跑 | 采集/枚举/保存 JPEG |
| `test_Draw_region` | ✅ 可跑 | LCD 参数 + 区域绘制 |
| `test_Trans_YUYV` | ✅ 可跑 | 合成 YUYV 色条 → RGB → LCD |
| `config_test` | ✅ 可跑 | 配置读取/打印 |
| `camera` | ✅ 可编译 | 当前只是配置检查占位，不是相机主程序 |
| `test_camera_preview` | ❌ 空文件，未加入 CMake | 下一步实现摄像头单帧预览 |

---

## 3. 板子与环境

### 3.1 板子

- 100ask i.MX6ULL；
- ARMv7 32-bit hard-float；
- 摄像头 `/dev/video1`；
- LCD `/dev/fb0`；
- 当前通过 ADB 连接，没有配置板子 IP。

板子上确认：

```bash
uname -a
ls -l /dev/video*
ls -l /dev/fb*
fbset -i
v4l2-ctl -d /dev/video1 --list-formats-ext
```

### 3.2 工具链

`cmake/toolchain-arm.cmake` 当前指向：

```text
/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot
```

编译器：

```text
bin/arm-buildroot-linux-gnueabihf-gcc
```

sysroot：

```text
arm-buildroot-linux-gnueabihf/sysroot
```

换机器/换 SDK 时先改这个文件。

### 3.3 构建

Release/Debug：

```bash
cd ~/Desktop/LinuxCamera

cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"

cmake -S . -B build-arm-dbg \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-arm-dbg -j"$(nproc)"
```

检查：

```bash
file build-arm/test_Trans_YUYV
grep CMAKE_C_COMPILER build-arm/CMakeCache.txt
```

### 3.4 部署

```bash
adb devices
adb shell mkdir -p /root/LinuxCamera/config

adb push build-arm/test_Get_images /root/LinuxCamera/
adb push build-arm/test_Draw_region /root/LinuxCamera/
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/

adb push config/camera.ini /root/LinuxCamera/config/
adb push config/lcd.ini /root/LinuxCamera/config/
adb push config/trans.ini /root/LinuxCamera/config/

adb shell
cd /root/LinuxCamera
chmod +x test_Get_images test_Draw_region test_Trans_YUYV
```

### 3.5 GDB

板子：

```bash
cd /root/LinuxCamera
gdbserver :1234 ./test_Trans_YUYV
```

Ubuntu：

```bash
adb forward tcp:1234 tcp:1234

GDB=$(find "$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot" \
      -type f -name '*-gdb' | head -n 1)

"$GDB" build-arm-dbg/test_Trans_YUYV
```

gdb：

```gdb
set pagination off
set confirm off
target remote 127.0.0.1:1234
b main
c
```

注意：

- 远程调试不用 `r`，用 `c`；
- 程序退出后 gdbserver 也结束；
- `adb forward` 失效时重新执行；
- 板子暂无 IP，走 ADB。

---

## 4. 当前代码架构

### 4.1 `camera_core`

CMake：

```cmake
add_library(camera_core STATIC
    src/config.c
    src/log.c
    src/lcd.c
    src/yuyv.c
)
```

它目前包含 Linux 专用的 `lcd.c`，所以整体是 Linux-only。

### 4.2 `config`

- `app_config_t` 同时包含 camera/storage/log/lcd/lcd_log/trans；
- 用 inih 解析；
- `config_load()` 返回 0/-1；
- 当前配置结构偏大，先不要大改。

### 4.3 `log`

- `LOG_DEBUG/INFO/WARN/ERROR`；
- 时间戳、文件/行号；
- 终端 + 文件；
- `errno` 保存/恢复；
- 无轮转；单线程。

### 4.4 `lcd`

API：

```c
int  lcd_init(char *lcd_cfg_path, app_config_t *lcd_cfg_p);
int  lcd_deinit(void);
int  lcd_put_pixel(unsigned int x, unsigned int y, unsigned int color);
void Get_lcd_parameters(struct fb_fix_screeninfo *fix,
                        struct fb_var_screeninfo *var);
```

- `lcd_init()` 读 lcd.ini、open `/dev/fb0`、ioctl、mmap；
- `lcd_put_pixel()` 支持 16/32bpp；
- 使用全局 `mem_addr` / `lcd_fix_screeninfo_t` / `lcd_var_screeninfo_t`；
- `lcd_deinit()` 会 `log_close()`，日志和 LCD 生命周期耦合。

### 4.5 `yuyv`

API：

```c
void Is_full_range(app_config_t *trans_cfg_p);
int  yuyv_to_rgb(unsigned char *yuyvdata, unsigned char *rgbdata,
                 int width, int height);
int  rgb_to_yuyv(...);
int  rgb_to_yuyv_pair(...);
unsigned char *generate_yuyv_region(int width, int height);
```

关键点：

- `Is_full_range()` 设置文件内 `static int full`；
- 调用 `yuyv_to_rgb()` 前**必须先调用** `Is_full_range()`；
- `yuyv_to_rgb()` 没有 `src_stride` 参数；
- `generate_yuyv_region()` 是测试用，但暴露在生产头文件里；
- 转换使用浮点，适合验证，不是最终性能实现。

---

## 5. 最近修复过的问题（避免重复踩）

1. **LCD 段错误**
   - `Get_lcd_parameters()` 只取参数、没有 mmap；
   - `lcd_put_pixel()` 写 `mem_addr == NULL`，第一次 `memcpy` 就段错误；
   - 修复：在 `lcd_init()` 中 mmap。

2. **颜色全白/异常**
   - `clamp()` 原来是 `void`，只改局部变量，没有修改传入值；
   - 负值赋给 `unsigned char` 回绕成 255；
   - 修复：`clamp(int *val)`，调用后生效。

3. **LIMIT 颜色异常**
   - 红色系数 `1.159` 应为 `1.596`；
   - 绿色 `-0.813*(V-128)` 的减号写成了加号；
   - 修复后 FULL/LIMIT 色条都正常。

4. **LCD 16bpp 写像素**
   - 早期用 `unsigned int *` 写 16bpp，覆盖下一个像素；
   - 修复：按 bpp 用 `uint16_t`/`uint32_t` + `memcpy`。

5. **`test_Draw_region` 结构**
   - 早期是测试文件兼库，`#if 1` 禁用 main；
   - 已拆出 `src/lcd.c` / `include/lcd.h`；
   - `test_Draw_region.c` 现在是手动测试程序。

6. **目录整理**
   - 已从 `LCD/`、`Trans/` 迁到 `src/`、`include/`、`tests/manual/`；
   - `test_camera_preview.c` 已创建但为空。

---

## 6. 当前最重要的任务：v0.1 摄像头单帧预览

### 6.1 目标

实现：

```text
摄像头 YUYV → yuyv_to_rgb() → lcd_put_pixel() → LCD
```

不做文件、不做缩放、不做连续预览。

### 6.2 新建/实现

```text
tests/manual/test_camera_preview.c
```

需要在 `CMakeLists.txt` 加 target：

```cmake
add_executable(test_camera_preview
    tests/manual/test_camera_preview.c
)
target_link_libraries(test_camera_preview PRIVATE camera_core)
```

### 6.3 流程

```text
config_load("config/camera.ini", &cfg)
lcd_init("config/lcd.ini", &lcd_cfg)

open(cfg.device)
VIDIOC_QUERYCAP
VIDIOC_S_FMT(YUYV, cfg.width, cfg.height)
读回实际 width/height/bytesperline/pixelformat
VIDIOC_REQBUFS
QUERYBUF + mmap
QBUF × N
STREAMON

poll
DQBUF
yuyv_to_rgb(mem[buf.index], rgb, actual_w, actual_h,
            actual_bytesperline, FULL)
逐像素 lcd_put_pixel()

QBUF
STREAMOFF
munmap
close
lcd_deinit()
```

### 6.4 必须先解决

- `yuyv_to_rgb()` 增加 `src_stride` 参数；
- 调用 `Is_full_range()` 或改成显式 range 参数；
- `S_FMT` 后读回实际格式；
- `REQBUFS` 后用实际 `count`；
- 摄像头分辨率 ≤ LCD 分辨率，否则先裁剪或换小分辨率；
- 打印 `bytesperline`、`line_length`、bpp。

### 6.5 验收

- [ ] 屏幕出现真实摄像头画面；
- [ ] 颜色基本正确；
- [ ] 无段错误；
- [ ] 程序能正常退出；
- [ ] 日志打印实际参数；
- [ ] 提交并打 `v0.1` 或 `v0.1-alpha` 标签。

---

## 7. 后续路线

### v0.1

- 单帧 YUYV → LCD。

### v0.5

- 连续采集；
- 双缓冲；
- FPS；
- 10 分钟不崩。

### v1.0

- 多分辨率；
- letterbox；
- 触摸拍照；
- JPEG 存卡；
- OSD；
- 8 小时稳定。

### v1.5 / v2.0

- HTTP MJPEG；
- 分层重构；
- 单元测试；
- Buildroot/自启/看门狗。

---

## 8. 已知代码/结构问题

不用一次修完，按优先级：

1. `test_camera_preview.c` 为空；
2. `yuyv_to_rgb()` 没有 stride；
3. `yuyv.c` 的全局 `full` 状态；
4. `lcd.c` 全局状态 + `log_close()` 耦合；
5. `include/lcd.h`、`include/yuyv.h` 不是自包含（依赖先 include `config.h`）；
6. `app_config_t` 过大；
7. `Get_lcd_parameters()` 名字和职责不完全一致；
8. `test_Get_images` 对 YUYV 仍保存 `.jpg`；
9. `scripts/` 当前不可直接执行；
10. `docs/performance.md` 为空；
11. 无单元测试/CI；
12. `README.md` 可能落后于代码，以 docs 为准。

---

## 9. 不要做的事

- 不要在 v0.1 之前做触摸/OSD/网络；
- 不要继续把生产代码写进测试文件；
- 不要为了“架构漂亮”大改 `config.c`；
- 不要手工 `arm-...gcc` 编译；
- 不要打 `v1.0` 标签；
- 不要用 Windows MSVC 编 V4L2 代码；
- 不要在摄像头帧未上屏前继续加新功能。

---

## 10. 新会话开始检查清单

1. 读本文；
2. `git status` / `git log --oneline -5`；
3. 看 `tests/manual/test_camera_preview.c` 是否仍为空；
4. 看 `CMakeLists.txt` 是否已有 `test_camera_preview` target；
5. 检查 `config/camera.ini` 的 `device/format/width/height/flag`；
6. 构建：

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"
```

7. 上板验证现有测试仍可跑；
8. 再开始 v0.1。

---

## 11. 命令速查

```bash
# 构建
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"

# 部署
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/
adb push config/*.ini /root/LinuxCamera/config/

# 运行
adb shell
cd /root/LinuxCamera
./test_Trans_YUYV

# gdb
gdbserver :1234 ./test_Trans_YUYV
adb forward tcp:1234 tcp:1234
"$GDB" build-arm-dbg/test_Trans_YUYV
```

---

## 12. 文档索引

| 文档 | 内容 |
|---|---|
| `README.md` | 项目入口、构建/运行 |
| `docs/architecture.md` | 模块、数据流、技术债 |
| `docs/board.md` | 板子、工具链、部署、gdb |
| `docs/roadmap.md` | 版本计划、验收 |
| `docs/debug-log.md` | 真实踩坑记录 |
| `docs/handoff.md` | 本文 |

---

## 13. 最后一句

> **当前不是“从零开始”，而是“零件齐全，差最后一条线”。**
> 下一步只做一件事：把摄像头 YUYV 帧送到 LCD，完成 v0.1。
