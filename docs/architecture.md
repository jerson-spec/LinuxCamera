# 架构说明

> 当前状态：**v0.1 Camera Preview**（摄像头 YUYV 单帧 → LCD）。
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
摄像头 YUYV 单帧 → yuyv_to_rgb() → lcd_put_pixel() → /dev/fb0
```

下一步：

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
    └── YUYV → 单帧预览
```

对应程序：

```text
tests/manual/test_Get_images.c
```

### 2.2 单帧摄像头预览（v0.1）

```text
open video
  → QUERYCAP
  → S_FMT(YUYV, 请求 1024×600)
  → 驱动实际返回 800×600
  → REQBUFS
  → mmap
  → QBUF × 4
  → STREAMON
  → poll
  → DQBUF
  → yuyv_to_rgb()
  → lcd_put_pixel()
  → QBUF
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
- 显示：左上角 800×600，右侧 224 像素黑色

原因：

- 摄像头不支持 1024×600；
- `VIDIOC_S_FMT` 会协商并调整；
- 代码读取 `vfmt.fmt.pix.width/height` 使用实际值；
- 1:1 显示，不缩放。

这不是 bug。铺满需要缩放/letterbox/裁剪，属于 v1.0。

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

这是当前技术债，但**v0.5 前不拆分**。

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
void Get_lcd_parameters(struct fb_fix_screeninfo *fix,
                        struct fb_var_screeninfo *var);
```

能力：

- 读 lcd.ini；
- open `/dev/fb0`；
- `FBIOGET_FSCREENINFO` / `FBIOGET_VSCREENINFO`；
- `mmap`；
- 16bpp RGB565 / 32bpp 写像素；
- `lcd_deinit()` 清理。

限制：

- 全局 `mem_addr`、`lcd_fix_screeninfo_t`、`lcd_var_screeninfo_t`；
- `lcd_deinit()` 先 close 再 munmap，并关闭日志；
- 错误路径清理不完整；
- 逐像素函数调用，性能不适合高帧率。

### 5.5 `tests/manual`

| 程序 | 作用 |
|---|---|
| `test_Get_images.c` | V4L2 采集/枚举/保存 JPEG |
| `test_Draw_region.c` | LCD 参数打印 + 绘制区域 |
| `test_Trans_YUYV.c` | 合成 YUYV 色条 → RGB → LCD |
| `test_camera_preview.c` | 摄像头 YUYV 单帧 → RGB → LCD（v0.1） |

### 5.6 构建目标

```text
camera_core          # config.c + log.c + lcd.c + yuyv.c
camera               # src/main.c，配置检查占位
test_Get_images      # 采集测试
test_Draw_region     # LCD 区域测试
test_Trans_YUYV      # YUYV 色条演示
test_camera_preview  # v0.1 单帧预览
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
9. `app_config_t` 暂不拆分，等 v0.5 稳定后再处理。

---

## 7. 已知技术债

按优先级：

1. 只有单帧预览，没有连续循环；
2. 没有双缓冲，连续预览会撕裂；
3. `lcd_put_pixel()` 逐像素，性能不足；
4. `yuyv_to_rgb()` 没有 stride 参数；
5. `yuyv.c` 全局 `full` 状态；
6. `lcd.c` 全局状态 + 日志生命周期耦合；
7. `test_Get_images` 对 YUYV 仍使用 `.jpg`；
8. `app_config_t` 过大；
9. 配置路径分散，安装后的 `/etc/...` 未接入；
10. 没有单元测试/CI；
11. `scripts/` 当前不可直接执行。

---

## 8. 下一步架构演进

### v0.5 连续预览

```text
test_camera_preview
  → while (running)
      poll
      DQBUF
      yuyv_to_rgb
      QBUF
      display
      FPS
```

注意：

- `QBUF` 要尽早；
- 先单缓冲跑通，再加双缓冲；
- 用 `FBIOPAN_DISPLAY` 切页；
- 测 `capture_ms` / `convert_ms` / `display_ms`；
- 性能不够时，先优化转换和显示，不要急着换板子。

### v1.0 前

- 把 V4L2 采集抽到 `src/camera.c`；
- 把显示循环抽到 `src/display.c`；
- `src/main.c` 只负责编排；
- 再考虑拆 `app_config_t`；
- 加 letterbox/缩放；
- 加触摸/OSD/JPEG。

---

## 9. 原则

- 先让画面连续动起来，再谈架构；
- 每次只改一个变量；
- 不为了“看起来专业”提前抽象；
- 不继续把生产代码写进测试文件；
- 不在 v0.5 前拆分 `app_config_t`。
