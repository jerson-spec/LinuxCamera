# 项目交接（Handoff）

> **新会话请先读本文。**
> 然后再读：
> - `docs/architecture.md`
> - `docs/board.md`
> - `docs/roadmap.md`
> - `docs/debug-log.md`
> - `docs/performance.md`
> - `README.md`
>
> 本文目标：让新的模型/新的你，在不看旧对话的情况下，快速知道
> **项目是什么、做到哪、怎么构建、怎么上板、下一步做什么、有哪些坑。**

---

## 0. 快速事实

| 项目 | 当前值 |
|---|---|
| 项目 | i.MX6ULL Linux 相机 |
| 当前阶段 | **v0.1 Camera Preview 已完成** |
| 语言 | C |
| 采集 | V4L2 MMAP |
| 显示 | `/dev/fb0` framebuffer |
| 摄像头 | `/dev/video1` |
| 摄像头请求 | 1024×600 YUYV |
| 摄像头实际 | **800×600 YUYV**（驱动调整） |
| `bytesperline` | **1600 = 800×2** |
| LCD | **1024×600** |
| 显示结果 | 左上角 800×600，右侧 224 像素黑色 |
| 工具链 | 100ask Buildroot SDK |
| 连接 | ADB，无板子 IP |
| 当前标签建议 | `v0.1` / `v0.1-camera-preview` |
| 下一步 | **v0.5 连续预览 + 双缓冲 + FPS** |
| 当前不是 | v0.5、v1.0 |

一句话：

> **摄像头 YUYV 单帧已经能显示到 LCD；下一步是连续预览。右侧黑边是分辨率不匹配，不是 bug。**

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
- 分辨率先 ≤ 800×480/800×600；
- 6ULL 无 GPU/VPU，后续可考虑 PXP；
- 先单摄像头；
- 核心数据通路自己写，CMake/ini/log 可以借鉴模板；
- 用户希望 AI 做 review、解释和规划，不直接代写核心数据通路。

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
- 合成 YUYV 色条上屏；
- **摄像头 YUYV 单帧 → RGB → LCD（v0.1）**。

### 2.3 还没做的功能

- 连续预览；
- 双缓冲；
- FPS；
- letterbox/缩放；
- 触摸/按键；
- OSD；
- libjpeg 拍照存卡；
- 网络推流；
- 8 小时稳定性；
- 单元测试/CI。

### 2.4 各程序状态

| 程序 | 状态 | 作用 |
|---|---|---|
| `test_Get_images` | ✅ | 采集/枚举/保存 JPEG |
| `test_Draw_region` | ✅ | LCD 参数 + 区域绘制 |
| `test_Trans_YUYV` | ✅ | 合成 YUYV 色条 → RGB → LCD |
| `test_camera_preview` | ✅ | 摄像头 YUYV 单帧 → LCD |
| `config_test` | ✅ | 配置读取/打印 |
| `camera` | ✅ 可编译 | 当前只是配置检查占位，不是正式主程序 |

---

## 3. 板子与环境

### 3.1 板子

- 100ask i.MX6ULL；
- ARMv7 32-bit hard-float；
- 摄像头 `/dev/video1`；
- LCD `/dev/fb0`，1024×600；
- 当前通过 ADB 连接，没有配置板子 IP。

确认：

```bash
uname -a
ls -l /dev/video*
ls -l /dev/fb*
fbset -i
v4l2-ctl -d /dev/video1 --list-formats-ext
```

### 3.2 摄像头实际分辨率

- 配置请求：1024×600；
- 驱动实际：**800×600 YUYV**；
- `bytesperline = 1600`；
- 摄像头不支持 1024×600；
- 所以右侧 224 像素黑色是正常的 1:1 显示。

### 3.3 工具链

`cmake/toolchain-arm.cmake`：

```text
SDK_ROOT=/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot
CC=${SDK_ROOT}/bin/arm-buildroot-linux-gnueabihf-gcc
SYSROOT=${SDK_ROOT}/arm-buildroot-linux-gnueabihf/sysroot
```

### 3.4 构建

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

如果 RelWithDebInfo 出现配置异常，先 `rm -rf build-arm` 重编，排除旧对象/ABI。

### 3.5 部署

```bash
adb devices
adb shell mkdir -p /root/LinuxCamera/config

adb push build-arm/test_camera_preview /root/LinuxCamera/
adb push build-arm/test_Get_images /root/LinuxCamera/
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/
adb push build-arm/test_Draw_region /root/LinuxCamera/

adb push config/camera.ini /root/LinuxCamera/config/
adb push config/lcd.ini /root/LinuxCamera/config/
adb push config/trans.ini /root/LinuxCamera/config/
```

板子：

```bash
cd /root/LinuxCamera
chmod +x test_camera_preview test_Get_images test_Trans_YUYV test_Draw_region
```

### 3.6 运行

```bash
./test_camera_preview
```

当前 `lcd.ini` 的 `display_time=300`，会保持 5 分钟；测试时改成 5 秒。

### 3.7 GDB

板子：

```bash
gdbserver :1234 ./test_camera_preview
```

Ubuntu：

```bash
adb forward tcp:1234 tcp:1234

GDB=$(find "$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot" \
      -type f -name '*-gdb' | head -n 1)

"$GDB" build-arm-dbg/test_camera_preview
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
- 程序退出后 gdbserver 结束；
- 当前板子没有 IP，走 ADB；
- `scripts/gdb.sh` 目前不完整，先用命令。

---

## 4. 当前代码架构

### 4.1 `camera_core`

```cmake
add_library(camera_core STATIC
    src/config.c
    src/log.c
    src/lcd.c
    src/yuyv.c
)
```

包含 Linux 专用 `lcd.c`，整体是 Linux-only。

### 4.2 `config`

- `app_config_t` 同时包含 camera/storage/log/lcd/lcd_log/trans；
- inih 解析；
- `config_load()` 返回 0/-1；
- 当前 `camera.ini` 请求 1024×600，但实际 800×600。

### 4.3 `log`

- DEBUG/INFO/WARN/ERROR；
- 时间戳、文件/行号；
- 终端 + 文件；
- `errno` 保存/恢复；
- 无轮转；单线程；
- `lcd_init()` 会重新初始化日志到 `lcd.log`；
- `lcd_deinit()` 会关闭日志。

### 4.4 `lcd`

API：

```c
int  lcd_init(char *lcd_cfg_path, app_config_t *lcd_cfg_p);
int  lcd_deinit(void);
int  lcd_put_pixel(unsigned int x, unsigned int y, unsigned int color);
void Get_lcd_parameters(struct fb_fix_screeninfo *fix,
                        struct fb_var_screeninfo *var);
```

- 读 lcd.ini；
- open `/dev/fb0`；
- ioctl fix/var；
- mmap；
- 16bpp RGB565 / 32bpp；
- 全局 `mem_addr` / fix / var。

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

- 文件内 `static int full`；
- 调用 `yuyv_to_rgb()` 前必须先 `Is_full_range()`；
- 没有 `src_stride`；
- 当前只支持 `bytesperline == width*2`；
- 浮点转换，适合验证，不是最终性能实现。

### 4.6 `test_camera_preview`

流程：

```text
config_load(camera.ini)
config_load(lcd.ini)
log_init
lcd_init
open camera
S_FMT YUYV
检查 bytesperline / 实际分辨率
REQBUFS / mmap / QBUF / STREAMON
poll / DQBUF
检查 bytesused
yuyv_to_rgb
lcd_put_pixel
QBUF
STREAMOFF / munmap
lcd_deinit
```

当前是**单帧**，显示 `lcd_display_time` 秒后退出。

---

## 5. 最近修复过的问题（避免重复踩）

1. **LCD 段错误**
   - `Get_lcd_parameters()` 只取参数、没有 mmap；
   - `lcd_put_pixel()` 写 `mem_addr == NULL`；
   - 修复：在 `lcd_init()` 中 mmap。

2. **颜色全白/异常**
   - `clamp()` 原来是 `void`，只改局部变量；
   - 负值赋给 `unsigned char` 回绕成 255；
   - 修复：`clamp(int *val)`。

3. **LIMIT 颜色异常**
   - `1.159` 应为 `1.596`；
   - `-0.813*(V-128)` 的减号写成加号；
   - 修复后 FULL/LIMIT 色条正常。

4. **16bpp 写像素**
   - 早期用 32-bit 指针写 16bpp，覆盖下一个像素；
   - 修复：按 bpp 用 `uint16_t`/`uint32_t` + `memcpy`。

5. **摄像头画面绿/雪花**
   - `camera.ini` 还是 `format=MJPG`，但预览按 YUYV 转；
   - 改成 `format=YUYV` 后正常。

6. **右侧黑边**
   - 请求 1024×600，摄像头实际 800×600；
   - `bytesperline=1600` 证明实际宽度 800；
   - 不是 bug，1:1 显示；缩放/letterbox 留 v1.0。

7. **RelWithDebInfo 配置异常**
   - 怀疑旧 build 缓存/头文件 ABI 或配置 CRLF；
   - 清 `build-arm` 重编；检查 `camera.ini` 字节。

---

## 6. 当前最重要的任务：v0.5 连续预览

### 6.1 目标

```text
while (running) {
    poll(timeout);
    DQBUF;
    yuyv_to_rgb;
    QBUF;
    display;
    fps;
}
```

### 6.2 先做单缓冲

- 连续循环；
- `poll` 超时；
- Ctrl-C 退出；
- FPS 统计；
- 接受撕裂。

### 6.3 再做双缓冲

- `yres_virtual = 2 * yres`；
- 两个显示 buffer；
- `FBIOPAN_DISPLAY`；
- 写后台，显示前台。

### 6.4 性能

当前逐像素 `lcd_put_pixel()` + 浮点转换，800×600 会慢。  
先测：

```text
fps
capture_ms
convert_ms
display_ms
```

再优化：

- YUYV → RGB565 直接写；
- 整数/查表；
- 按行写；
- 减少中间 RGB888 buffer。

### 6.5 验收

- [ ] 连续 10 分钟不崩；
- [ ] FPS 可打印；
- [ ] 无内存增长；
- [ ] Ctrl-C 干净退出；
- [ ] 双缓冲可用。

---

## 7. 后续路线

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

按优先级：

1. 只有单帧，没有连续循环；
2. 没有双缓冲；
3. `lcd_put_pixel()` 逐像素，性能不足；
4. `yuyv_to_rgb()` 没有 stride；
5. `yuyv.c` 全局 `full`；
6. `lcd.c` 全局状态 + 日志生命周期耦合；
7. `test_Get_images` 对 YUYV 仍用 `.jpg`；
8. `app_config_t` 过大；
9. 配置路径分散；
10. `scripts/` 不可直接执行；
11. 无单元测试/CI；
12. `README` 已更新，但以 docs 为准。

---

## 9. `app_config_t` 决策

**当前不拆分。**

理由：

- 不是当前 bug 的原因；
- 会同时改 config.c、所有测试、lcd/yuyv 接口；
- v0.1 刚完成，先做 v0.5；
- 等触摸/OSD/多分辨率需求明确后再拆。

在 `docs/architecture.md` 已留 TODO。

---

## 10. 不要做的事

- 不要在 v0.5 前做触摸/OSD/网络；
- 不要继续把生产代码写进测试文件；
- 不要为了“架构漂亮”大改 `config.c`；
- 不要手工 `arm-...gcc` 编译；
- 不要打 `v1.0` 标签；
- 不要用 Windows MSVC 编 V4L2；
- 不要在未确认实际分辨率时假设 1024×600。

---

## 11. 新会话开始检查清单

1. 读本文；
2. `git status` / `git log --oneline -5`；
3. 确认 `test_camera_preview.c` 已实现；
4. 确认 `config/camera.ini` 的 `format=YUYV`；
5. 确认摄像头实际 800×600、LCD 1024×600；
6. 构建：

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"
```

7. 上板验证现有程序；
8. 再开始 v0.5。

---

## 12. 命令速查

```bash
# 构建
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"

# 部署
adb push build-arm/test_camera_preview /root/LinuxCamera/
adb push config/*.ini /root/LinuxCamera/config/

# 运行
adb shell
cd /root/LinuxCamera
./test_camera_preview

# gdb
gdbserver :1234 ./test_camera_preview
adb forward tcp:1234 tcp:1234
"$GDB" build-arm-dbg/test_camera_preview
```

---

## 13. 文档索引

| 文档 | 内容 |
|---|---|
| `README.md` | 项目入口、构建/运行 |
| `docs/architecture.md` | 模块、数据流、技术债 |
| `docs/board.md` | 板子、工具链、部署、gdb |
| `docs/roadmap.md` | 版本计划、验收 |
| `docs/performance.md` | 性能指标、优化顺序 |
| `docs/debug-log.md` | 真实踩坑记录 |
| `docs/handoff.md` | 本文 |
| `docs/handoff-prompt.md` | 新会话启动提示词 |

---

## 14. 最后一句

> **v0.1 已完成；下一步是让画面连续动起来，而不是继续修黑边。**
