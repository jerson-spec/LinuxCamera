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
| 当前阶段 | **v1.0 完成（8 小时长跑通过）** |
| 语言 | C |
| 采集 | V4L2 MMAP（4 buffer，poll 超时 2 s，`VIDIOC_S_PARM` 请求帧率） |
| 显示 | `/dev/fb0` framebuffer + 双缓冲 + letterbox |
| 摄像头 | `/dev/video1` |
| 摄像头请求 | 1024×600 YUYV |
| 摄像头实际 | **800×600（15 fps）/ 640×480（30 fps）/ 352×288（30 fps）** |
| `bytesperline` | **1600 = 800×2**（640×480 时 1280） |
| LCD | **1024×600，32bpp，`line_length = 4096`** |
| 双缓冲 | `yres_virtual = 1200`，`FBIOPAN_DISPLAY`，失败自动降级 |
| 显示结果 | letterbox 居中；800×600 时左右各 112 像素黑边 |
| 实测 | 800×600：conv 14.0，draw 8.6，CPU 34%；640×480：conv 9.4，draw 5.5 |
| 交互 | 触摸：左半边拍照、右半边切分辨率；`SIGUSR1`/`SIGUSR2` 也能触发 |
| 拍照 | 切 MJPG 取一帧直接写 `.jpg`，46–48 KB（800×600）/ 24 KB（640×480） |
| 长跑 | 8 h，610390 帧，0 错误，VmRSS 不涨，fd=7 不变，温度 68–71℃ |
| 工具链 | 100ask Buildroot SDK |
| 连接 | ADB，无板子 IP |
| 当前标签 | `v1.0` |
| 下一步 | **v1.5：MJPEG over HTTP；之后进内核/驱动** |
| 当前不是 | v2.0 |

一句话：

> **v1.0 已经能跑：预览、居中、触摸拍照、切分辨率、OSD，8 小时不掉线；下一步是联网和驱动。**

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
│   ├── release-v1.0.md
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
- **摄像头 YUYV 单帧 → RGB → LCD（v0.1）**；
- **连续预览 + 每秒 FPS/耗时/丢帧/VmRSS 统计（v0.5）**；
- **双缓冲：`FBIOPAN_DISPLAY` 切页，pan 失败自动降级单缓冲（v0.5）**；
- **YUYV→RGB 定点化 + 按行写 framebuffer（W3，conv 14.0 / draw 8.6）**；
- **letterbox 居中显示（W3）**；
- **640×480@30 已验证（`VIDIOC_S_PARM` 请求帧率）**；
- **触摸输入：`touch.c`，左半边拍照、右半边切分辨率（v1.0）**；
- **OSD：`osd.c`，8×16 点阵，显示分辨率/帧率/丢帧（v1.0）**；
- **8 小时长跑通过（610390 帧、0 错误、RSS/fd 不增长）**。

### 2.3 还没做的功能

- 网络推流（v1.5）；
- 缩放（放大/缩小）；
- 分层重构、单元测试、Buildroot 精简、开机自启、看门狗（v2.0）；
- 内核/驱动方向（W8–W9）。

### 2.4 各程序状态

| 程序 | 状态 | 作用 |
|---|---|---|
| `test_Get_images` | ✅ | 采集/枚举/保存 JPEG（文件名用格式名作后缀） |
| `test_Draw_region` | ✅ | LCD 参数 + 区域绘制 |
| `test_Trans_YUYV` | ✅ | 合成 YUYV 色条 → RGB → LCD |
| `test_camera_preview` | ✅ | **v1.0 主程序**：预览 + 触摸 + 拍照 + 切分辨率 + OSD |
| `soak_test.sh` | ✅ | 长跑脚本，定时拍照/切分辨率/采样 |
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

- 配置请求：1024×600（默认）；
- 驱动实际：**800×600 YUYV**；
- `bytesperline = 1600`；
- 摄像头不支持 1024×600；
- 画面用 letterbox 居中，左右各 112 像素黑边；
- 把配置改成 640×480 可以跑 30fps（摄像头在这个尺寸下支持 30/25/20/15/10/5）。

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

按 Ctrl-C 结束，程序会打印 `DQbuf = Qbuf = Frames` 后退出。
`lcd.ini` 里的 `display_time` 从 v0.5 起对预览无效（现在由 Ctrl-C 结束）。

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
    src/camera.c
    src/osd.c
    src/touch.c
)
```

包含 Linux 专用 `lcd.c` / `camera.c` / `touch.c`，整体是 Linux-only。

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
int  lcd_flip(void);                 /* 切前后台缓冲，成功 0 / 失败 -1 */
int  lcd_doublebuffering_on(void);   /* 1 = 双缓冲已启用 */
int  lcd_write_line_rgb888(int line, int x_offset,
                           unsigned char *rgb, int width);  /* 按行直写 32bpp */
void Get_lcd_parameters(struct fb_fix_screeninfo *fix,
                        struct fb_var_screeninfo *var);
```

- 读 lcd.ini；
- open `/dev/fb0`；
- ioctl fix/var；
- `FBIOPUT_VSCREENINFO` 设 `yres_virtual = 2 * yres` 并回读校验；
- mmap（32 MB 预留区）；
- 16bpp RGB565 / 32bpp；
- `lcd_flip()` 用 `FBIOPAN_DISPLAY` 切页，失败自动降级；
- `lcd_write_line_rgb888()` 是预览快路径：一行只算一次地址，只支持 32bpp；
- 全局 `mem_addr` / fix / var / `draw_offset`。

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

当前是**连续循环**（v0.5 起）：每秒打印一次统计，按 Ctrl-C 结束。
上面这段是 v0.1 的流程；v0.5 在 `yuyv_to_rgb` 之后会先 `QBUF`，再用
`lcd_write_line_rgb888` 按行写、`lcd_flip` 切页，然后循环。

v1.0 的循环多了三块：

```text
poll(摄像头, 触摸, 2000ms)
  → 触摸：左半边 g_save=1，右半边 g_res=1
  → 取帧 / 转换 / QBUF / 按行写 / OSD / flip / 统计
  → g_save：snapshot_mjpeg()，切 MJPG 抓一帧存 jpg，再切回 YUYV
  → g_res ：res_switch()，轮换 800×600 / 640×480 / 352×288
```

### 4.7 `camera`

`include/camera.h` / `src/camera.c`，纯 V4L2，状态在调用者的 `camera_t` 里：

```c
int  camera_open(camera_t *cam, const char *dev);
int  camera_set_format(camera_t *cam, unsigned int w, unsigned int h, unsigned int pixfmt);
int  camera_set_fps(camera_t *cam, unsigned int num, unsigned int den);
int  camera_set_fps_if_needed(camera_t *cam, unsigned int num, unsigned int den);
int  camera_start(camera_t *cam, unsigned int nbufs);
int  camera_dequeue(camera_t *cam, struct v4l2_buffer *buf);
int  camera_queue(camera_t *cam, struct v4l2_buffer *buf);
int  camera_stop(camera_t *cam);
void camera_close(camera_t *cam);
```

- `set_format` 在 streaming 时直接拒绝，调用者必须先 `stop`；
- `stop` 做 `STREAMOFF → munmap → REQBUFS(0)`，可重复调用；
- `set_fps_if_needed` 先 `G_PARM`，帧率已经对就不碰 `S_PARM`。

### 4.8 `osd`

`include/osd.h` / `src/osd.c`，8×16 点阵，直接调 `lcd_put_pixel()`：

```c
int osd_show_text(x, y, str, color);
int osd_text_width(str);
int osd_show_text_right(right_x, y, str, color);
int osd_show_text_shadow(x, y, str, color, shadow_color);
int osd_show_rect(x, y, w, h, color);
```

文字必须画在 `lcd_flip()` 之前（后台 buffer）。

### 4.9 `touch`

`include/touch.h` / `src/touch.c`，evdev 单点：

```c
int  touch_open(touch_t *t, const char *dev);
void touch_set_range(touch_t *t, int x_min, int x_max, int y_min, int y_max);
int  touch_read(touch_t *t, int *x, int *y);   /* -1 错误 / 0 无点击 / 1 点击 */
void touch_close(touch_t *t);
```

- 打开时 `O_NONBLOCK`，由主循环 `poll` 驱动；
- 优先 `ABS_X`，不支持时退回 `ABS_MT_POSITION_X`；
- 这颗屏驱动报 800×480，实际发 1024×600，所以 `main` 里用 `touch_set_range()` 覆盖。

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
   - 不是 bug；v0.1 先 1:1 显示，W3 改成 letterbox 居中（左右各 112 像素）。

7. **RelWithDebInfo 配置异常**
   - 怀疑旧 build 缓存/头文件 ABI 或配置 CRLF；
   - 清 `build-arm` 重编；检查 `camera.ini` 字节。

---

## 6. 连续预览（v0.5 → v1.0）

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

### 6.5 验收（已全部通过，2026-11-08）

- [x] 连续 10 分钟不崩（604 s / 9036 帧）；
- [x] FPS 可打印（15.0）；
- [x] 无内存增长（VmRSS 6324 kB 恒定）；
- [x] Ctrl-C 干净退出；
- [x] 双缓冲可用（`FBIOPAN_DISPLAY` 全程无错误，blank 后可复现失败并降级）。

---

### 6.6 v1.0：拍照 + 触摸 + OSD + 8 小时（已完成，2026-11-14）

v0.5 之后又加了三块，最后用 8 小时长跑收尾。

**新增的模块**

- `src/camera.c`：V4L2 采集抽出来，`camera_t` 由调用者持有；
- `src/osd.c`：8×16 点阵，显示分辨率/帧率/丢帧；
- `src/touch.c`：evdev 单点，左半边拍照、右半边切分辨率；
- `scripts/soak_test.sh`：定时拍照/切分辨率/采样。

**拍照**

走的是“切格式”而不是软件编码：`STOP → S_FMT(MJPG) → START → 取一帧 → 写 .jpg → 切回 YUYV`。
800×600 约 763–767 ms、33–34 KB；640×480 约 523–525 ms、24–25 KB。
`S_PARM` 必须在 `STOP` 之后、`START` 之前调，否则 EBUSY。

**切分辨率**

800×600 / 640×480 / 352×288，`SIGUSR2` 或点右半边触发。
耗时：640×480 约 620 ms、352×288 约 490 ms、800×600 约 1.7 s（USB 带宽重新协商）。
`lb` 和 `rgbbuf` 只在切换成功后一次性提交，失败路径不动它们。

**8 小时**

19:38 → 03:38，610390 帧、47 张照片、15 次切换、0 错误、`DQbuf = Qbuf = Frames`。
VmRSS 在 1472–6548 kB 之间摆但下限不变，fd 恒为 7，温度 68–70℃。
细节在 `docs/performance.md` 的 2026-11-14 一节。

**验收**

- [x] 3 种分辨率切换不花屏；
- [x] 触摸能拍照；
- [x] PC 能打开照片；
- [x] OSD 正常；
- [x] 8 小时不崩。

---

## 7. 后续路线

v1.0 已完成，下面只写还没做的。

### v1.5 网络（下一步）

- [ ] 板子联网：`udhcpc -i eth0` 或静态 IP，Ubuntu 能 ping 通；
- [ ] 最小 HTTP 服务：`src/http.c`，单客户端，先支持 `GET /snapshot.jpg`；
- [ ] 把 `snapshot_mjpeg()` 拆成“取一帧到内存”和“写文件”，HTTP 返回内存里的 JPEG；
- [ ] `GET /stream`：`multipart/x-mixed-replace` 推 MJPEG；
- [ ] 处理 `SIGPIPE`、部分写、客户端断开；
- [ ] 推流时摄像头是 MJPG，本地预览会停——接受，并在 README 写清楚；
- [ ] 延迟 < 300 ms，断开重连不崩；
- [ ] 打 `v1.5`。

### v2.0 工程化 + 驱动

- [ ] `test_camera_preview.c` 拆成 `src/app.c` + `display.c`；
- [ ] 拆 `app_config_t`，给 http/touch/osd 加配置；
- [ ] 单元测试：`yuyv_to_rgb`、config 解析可以脱离硬件测；
- [ ] Buildroot 精简镜像；
- [ ] 开机自启 + 看门狗；
- [ ] 驱动方向：字符设备 → poll/等待队列 → input → I2C/regmap → V4L2/UVC 内核代码。

---

## 8. 已知代码/结构问题

按优先级：

1. `test_camera_preview.c` 700 行，采集/显示/触摸/OSD/拍照/切分辨率全在里面，v2.0 前要拆；
2. `lcd_write_line_rgb888()` 只支持 32bpp，其他 bpp 直接返回失败；
3. `yuyv_to_rgb()` 没有 `src_stride`，只支持 `bytesperline == width*2`；
4. `yuyv.c` 全局 `full`；
5. `lcd.c` 全局状态；`lcd_init()` 重置日志、`lcd_deinit()` 关闭日志；
6. `test_Get_images` 文件名直接拼格式名（`.MJPG` / `.YUYV`）；
7. `app_config_t` 过大，还没有 touch/OSD 的配置项；
8. 配置路径分散，`lcd_init` 会重复 load `lcd.ini`；
9. `src/main.c` 仍是占位程序；
10. 首帧延迟 1–1.6 s；切到 800×600 固定 1.7 s；
11. 无单元测试/CI；`scripts/` 里只有 build/deploy/gdb/soak，没有统一入口。

---

## 9. `app_config_t` 决策

**v1.0 仍然不拆，v1.5 只在原结构里加字段，v2.0 再拆。**

理由：

- v1.0 的功能都已经跑通，现在拆只会引入回归；
- v1.5 只加 `[http]` 一节（端口、root、是否允许推流），在原结构里加几个字段最省事；
- 等 v1.5 稳定、v2.0 重构时，再按 camera / lcd / log / http 拆成独立结构体；
- 拆分要同时改 `config.c`、所有测试、`lcd_init`/`yuyv` 接口，属于 v2.0 的活。

在 `docs/architecture.md` 已留 TODO。

---

## 10. 不要做的事

- 不要在 v1.5 验收前动 v2.0 的分层重构；
- 不要继续把生产代码写进测试文件（新功能进 `src/`）；
- 不要为了“架构漂亮”大改 `config.c`；
- 不要手工 `arm-...gcc` 编译；
- 不要在 v1.5 验收前打 `v1.5` 标签；
- 不要假设摄像头能同时输出 YUYV 和 MJPG（只有一路）；
- 不要在推流时忘了处理 `SIGPIPE` 和客户端断开；
- 不要用 Windows MSVC 编 V4L2；
- 不要在未确认实际分辨率时假设 1024×600；
- 不要把 `camera.log`、`soak/`、`images/` 提交进仓库。

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

7. 上板跑 30 秒，确认 15fps、`DQbuf == Qbuf == Frames`、无 ERROR；
8. 需要长跑时：`nohup ./test_camera_preview &` + `nohup ./soak_test.sh 8 &`；
9. 再开始 v1.5（网络）或 W8–W9（驱动）。

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

# 长跑
nohup ./test_camera_preview >/dev/null 2>&1 &
nohup ./soak_test.sh 8 >/dev/null 2>&1 &
tail -f soak/soak_*.log
```

---

## 13. 文档索引

| 文档 | 内容 |
|---|---|
| `README.md` | 项目入口、构建/运行 |
| `docs/architecture.md` | 模块、数据流、技术债 |
| `docs/board.md` | 板子、工具链、部署、gdb、长跑 |
| `docs/roadmap.md` | 版本计划、验收 |
| `docs/performance.md` | 性能指标、8 小时长跑数据 |
| `docs/release-v1.0.md` | v1.0 发布说明（可直接贴到 GitHub Release） |
| `docs/debug-log.md` | 真实踩坑记录 |
| `docs/handoff.md` | 本文 |

---

## 14. 最后一句

> **v1.0 已经能演示了：预览、居中、触摸拍照、切分辨率、OSD，8 小时不掉线；下一步是联网和驱动。**
