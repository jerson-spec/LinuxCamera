# 架构说明

> 当前状态：**W3 完成**（连续预览 + 双缓冲 + 定点转换 + 按行写 + letterbox）。
>
> 本文描述当前真实代码结构，不描述理想中的最终架构。
> 新会话请先读 `docs/handoff.md`，再读本文。

---

## 1. 项目定位

基于 i.MX6ULL + Linux 的相机项目。

目标链路：

```text
V4L2 采集 → YUYV 转换 → LCD 显示 → 拍照/存卡 → 网络推流
```

当前已经完成：

```text
摄像头 YUYV → yuyv_to_rgb()（定点）→ 按行写 framebuffer → 双缓冲 flip
每秒统计 fps/耗时/丢帧/内存；支持 800×600@15 / 640×480@30 和 letterbox 居中
```

```text
连续预览 + 双缓冲 + FPS
```

---

## 2. 当前数据流

### 2.1 采集/保存

```text
/dev/video1
    │
    ▼
V4L2 MMAP 采集
    │
    ├── MJPEG → 保存 .jpg
    │
    └── YUYV → 连续预览
```

对应程序：

```text
tests/manual/test_Get_images.c
```

### 2.2 摄像头预览（v0.1 单帧 → v0.5 循环）

```text
open video
  → QUERYCAP
  → S_FMT(YUYV, 请求 1024×600)
  → 驱动实际返回 800×600（或 640×480）
  → VIDIOC_S_PARM 请求 30 fps（驱动按支持情况给）
  → REQBUFS(4)
  → mmap
  → QBUF × 4
  → STREAMON
  → poll(2000ms) → DQBUF
  → yuyv_to_rgb()（定点）
  → QBUF（尽早归还）
  → 算居中偏移 (xres-w)/2、(yres-h)/2
  → 逐行 lcd_write_line_rgb888(y0+i, x0, ...)
  → lcd_flip()（FBIOPAN_DISPLAY）
  → 每秒统计 fps/wait/conv/draw/dropped/rss
  → 循环直到 Ctrl-C
  → STREAMOFF
  → munmap
```

对应程序：

```text
tests/manual/test_camera_preview.c
```

### 2.3 合成色条测试

```text
generate_yuyv_region()
  → yuyv_to_rgb()
  → lcd_put_pixel()
  → /dev/fb0
```

对应程序：

```text
tests/manual/test_Trans_YUYV.c
```

---

## 3. 当前实际分辨率

板子：

- 摄像头请求：`1024×600`
- 摄像头实际：`800×600` YUYV
- `bytesperline = 1600 = 800 × 2`
- LCD：`1024×600`
- 显示：letterbox 居中，800×600 时左右各 112 像素黑色

原因：

- 摄像头不支持 1024×600；
- `VIDIOC_S_FMT` 会协商并调整；
- 代码读取 `vfmt.fmt.pix.width/height` 使用实际值；
- W3 起用 letterbox 居中，不缩放。

---

## 4. 目录职责

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
├── scripts/
├── build-arm/
└── stage/
```

| 路径 | 职责 |
|---|---|
| `src/` | 生产代码，不带 `main` |
| `include/` | 公共头文件 |
| `tests/manual/` | 需要板子/摄像头/LCD 的手动测试 |
| `tests/unit/` | 无硬件依赖的自动测试 |
| `config/` | ini 配置 |
| `third_party/` | 第三方 inih |
| `cmake/` | 交叉工具链 |
| `docs/` | 架构、板子、路线、交接、调试记录 |
| `scripts/` | 构建/部署/gdb 辅助脚本（当前待修） |
| `build-*/`、`stage/` | 生成物，不提交 |

---

## 5. 模块说明

### 5.1 `config`

文件：

```text
include/config.h
src/config.c
```

职责：

- inih 解析 ini；
- 字符串转 `app_config_t`；
- 范围校验；
- 打印最终配置。

`app_config_t` 当前包含：

```text
camera / storage / log / lcd / lcd_log / trans
```

这是当前技术债。

### 5.2 `log`

文件：

```text
include/log.h
src/log.c
```

能力：

- DEBUG / INFO / WARN / ERROR；
- 时间戳；
- `文件:行号`；
- 终端 + 文件；
- `errno` 保存/恢复；
- `flockfile` 防多线程交错。

限制：

- 无日志轮转；
- 单线程；
- `lcd_init()` 会重新 `log_init()` 到 `lcd.log`；
- `lcd_deinit()` 会 `log_close()`，日志生命周期与 LCD 耦合。

### 5.3 `yuyv`

文件：

```text
include/yuyv.h
src/yuyv.c
```

能力：

- `yuyv_to_rgb()`：YUYV → RGB888；
- `rgb_to_yuyv()`：RGB888 → YUYV；
- `rgb_to_yuyv_pair()`：两个 RGB 像素 → 4 字节；
- `generate_yuyv_region()`：8 色条；
- `Is_full_range()`：设置 FULL / LIMIT。

关键限制：

- 使用文件内 `static int full`；
- 调用 `yuyv_to_rgb()` 前必须先调用 `Is_full_range()`；
- `yuyv_to_rgb()` 没有 `src_stride` 参数；
- 当前只支持 `bytesperline == width*2`；
- 转换使用浮点，适合验证，不是最终性能实现。

### 5.4 `lcd`

文件：

```text
include/lcd.h
src/lcd.c
```

API：

```c
int  lcd_init(char *lcd_cfg_path, app_config_t *lcd_cfg_p);
int  lcd_deinit(void);
int  lcd_put_pixel(unsigned int x, unsigned int y, unsigned int color);
int  lcd_flip(void);                 /* FBIOPAN_DISPLAY 切页 */
int  lcd_doublebuffering_on(void);
int  lcd_write_line_rgb888(int line, int x_offset,
                           unsigned char *rgb, int width);
void Get_lcd_parameters(struct fb_fix_screeninfo *fix,
                        struct fb_var_screeninfo *var);
```

能力：

- 读 lcd.ini；
- open `/dev/fb0`；
- `FBIOGET_FSCREENINFO` / `FBIOGET_VSCREENINFO`；
- 设 `yres_virtual = 2 * yres`，PUT 后回读校验；
- `mmap`；
- 16bpp RGB565 / 32bpp 写像素；
- `lcd_flip()` 切前后台缓冲，pan 失败自动降级；
- `lcd_deinit()` 清理。

限制：

- 全局 `mem_addr`、`lcd_fix_screeninfo_t`、`lcd_var_screeninfo_t`、`draw_offset`；
- `lcd_deinit()` 先 close 再 munmap，并关闭日志；
- 错误路径清理不完整；
- 逐像素函数调用，性能不适合高帧率（480k 次/帧，约 30 ms）。

### 5.5 `tests/manual`

| 程序 | 作用 |
|---|---|
| `test_Get_images.c` | V4L2 采集/枚举/保存 JPEG |
| `test_Draw_region.c` | LCD 参数打印 + 绘制区域 |
| `test_Trans_YUYV.c` | 合成 YUYV 色条 → RGB → LCD |
| `test_camera_preview.c` | 连续预览 + 双缓冲 + 统计（v0.5 / W3） |

### 5.6 构建目标

```text
camera_core          # config.c + log.c + lcd.c + yuyv.c
camera               # src/main.c，配置检查占位
test_Get_images      # 采集测试
test_Draw_region     # LCD 区域测试
test_Trans_YUYV      # YUYV 色条演示
test_camera_preview  # 连续预览（v0.5，W3 优化）
config_test          # 配置测试
```

`camera_core` 目前包含 Linux 专用的 `lcd.c`，所以整体是 Linux-only。

---

## 6. 关键设计决策

1. 语言用 C，不用 C++/Qt；
2. 采集用 V4L2 MMAP，不用 `read()`；
3. 预览用 YUYV，拍照用 MJPEG；
4. 显示先用 `/dev/fb0`，不直接上 DRM；
5. 配置用 ini + inih；
6. 日志自己写，不引入 spdlog/zlog；
7. 先单帧，后连续；先单缓冲，后双缓冲；
8. 不提前做 Qt/LVGL/MQTT/RTSP；
9. `app_config_t` 暂不拆分。

---

## 7. 已知技术债

按优先级：

1. `yuyv_to_rgb()`  没有 stride 参数；
2. `yuyv.c` 全局 `full` 状态；
3. `lcd.c` 全局状态；`lcd_init()` 重置日志、`lcd_deinit()` 关闭日志；
4. `test_camera_preview.c` 550+ 行，承担主程序职责；
5. `app_config_t` 过大；
6. 配置路径分散，`lcd_init()` 会重复 load `lcd.ini`；
7. 首帧延迟 1–1.6 s；
8. 没有单元测试/CI；

---

## 8. 下一步架构演进

### W3 已完成（2026-11-09）

```text
优化前：conv 35.1 + draw 30.6 = 65.7 ms / 66.7 ms（CPU 98%）
优化后：conv 14.0 + draw  8.6 = 22.6 ms / 66.7 ms（CPU 34%）

结果：640×480@30 跑通；letterbox 居中
```

### 下一步：v1.0 拍照

- 先把 `test_camera_preview.c` 里的拍照逻辑单独拆成函数（触发源先用信号，
  后面换触摸）；
- 用 libjpeg 或摄像头 MJPEG 存盘，文件写到 `images/`；
- 拍照时不要改预览主循环的结构，触发只置标志。

### v1.0 前要做的小重构

- 把 V4L2 采集抽到 `src/camera.c`；
- 把显示循环抽到 `src/display.c`；
- `src/main.c` 只负责编排；
- 日志只在 main 初始化一次，`lcd.c` 不再碰日志；
- 再考虑拆 `app_config_t`。

---

## 9. 原则

- 先让画面连续动起来，再谈架构；
- 每次只改一个变量；
- 不为了“看起来专业”提前抽象；
- 不继续把生产代码写进测试文件；
