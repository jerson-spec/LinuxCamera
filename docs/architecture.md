# 架构说明

> 本文描述当前代码的实际结构，不描述“理想中的最终架构”。
> 当前阶段：LCD / YUYV Milestone（pre-v0.1）。
> 新会话请先读 `docs/handoff.md`，再读本文。

---

## 1. 项目定位

基于 i.MX6ULL + Linux 的相机项目，目标是逐步完成：

```text
V4L2 采集 → YUYV 转换 → LCD 显示 → 拍照/存卡 → 网络推流
```

当前已经完成采集、配置、日志、LCD 显示和 YUYV 转换的“分段验证”，
但还没有把摄像头帧直接送到 LCD。

当前主线是：

```text
摄像头 YUYV 帧 → yuyv_to_rgb() → lcd_put_pixel() → /dev/fb0
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
    └── YUYV → 下一阶段用于显示
```

对应程序：

```text
tests/manual/test_Get_images.c
```

### 2.2 LCD 色条测试

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

对应程序：

```text
tests/manual/test_Trans_YUYV.c
```

### 2.3 目标数据流（下一步）

```text
DQBUF
  │
  ▼
mem[buf.index]（YUYV，src_stride）
  │
  ▼
yuyv_to_rgb()
  │
  ▼
lcd_put_pixel() / lcd_put_rgb565()
  │
  ▼
QBUF
```

---

## 3. 目录职责

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
│   │   └── test_camera_preview.c   # 目前为空，下一步实现
│   └── unit/
│       └── config_test.c
├── third_party/
│   └── inih/
├── docs/
├── scripts/
├── build-arm/
└── stage/
```

职责：

| 路径 | 职责 |
|---|---|
| `src/` | 生产代码，不带 `main` |
| `include/` | 公共头文件 |
| `tests/manual/` | 需要板子/摄像头/LCD 的手动测试程序 |
| `tests/unit/` | 无硬件依赖的自动测试 |
| `config/` | ini 配置 |
| `third_party/` | 第三方 inih |
| `cmake/` | 交叉工具链 |
| `docs/` | 架构、板子、路线、交接 |
| `scripts/` | 构建/部署/gdb 辅助脚本 |
| `build-*/`、`stage/` | 生成物，不提交 |

---

## 4. 模块说明

### 4.1 `config`

文件：

```text
include/config.h
src/config.c
```

职责：

- 用 inih 解析 ini；
- 将字符串转成 `app_config_t`；
- 做范围校验；
- 打印最终配置。

当前 `app_config_t` 同时包含：

```text
camera / storage / log / lcd / lcd_log / trans
```

这是当前阶段的技术债：配置结构偏大，后续可以按模块拆分或嵌套。

### 4.2 `log`

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

当前限制：

- 无日志轮转；
- 单线程写文件；
- `lcd_init()` 和 `lcd_deinit()` 会调用 `log_init()` / `log_close()`，日志生命周期和 LCD 耦合。

### 4.3 `yuyv`

文件：

```text
include/yuyv.h
src/yuyv.c
```

能力：

- `yuyv_to_rgb()`：YUYV → RGB888；
- `rgb_to_yuyv()`：RGB888 → YUYV；
- `rgb_to_yuyv_pair()`：两个 RGB 像素 → 4 字节 YUYV；
- `generate_yuyv_region()`：合成 8 色条；
- `Is_full_range()`：设置 FULL / LIMIT。

当前限制：

- 使用文件内 `static int full` 保存范围，转换函数依赖先调用 `Is_full_range()`；
- `yuyv_to_rgb()` 没有 `src_stride` 参数，默认数据紧凑；
- 测试用的 `generate_yuyv_region()` 和 `rgb_to_yuyv_pair()` 暴露在生产头文件中；
- 转换使用浮点，适合验证，后续高帧率应改整数/查表。

### 4.4 `lcd`

文件：

```text
include/lcd.h
src/lcd.c
```

能力：

- `lcd_init(path, cfg)`：读 lcd.ini、open `/dev/fb0`、ioctl、mmap；
- `lcd_deinit()`：清理；
- `lcd_put_pixel()`：支持 16bpp RGB565 / 32bpp；
- `Get_lcd_parameters()`：把 fix/var 信息拷贝给调用者。

当前限制：

- 使用全局 `mem_addr`、`lcd_fix_screeninfo_t`、`lcd_var_screeninfo_t`；
- `lcd_deinit()` 先 `close(fd)` 再 `munmap()`，并顺带 `log_close()`；
- 错误路径没有完整清理；
- 每像素函数调用，适合单帧测试，不适合高帧率。

### 4.5 `tests/manual`

| 程序 | 作用 |
|---|---|
| `test_Get_images.c` | V4L2 采集/枚举/保存 JPEG |
| `test_Draw_region.c` | LCD 参数打印 + 绘制区域 |
| `test_Trans_YUYV.c` | 合成 YUYV 色条 → RGB → LCD |
| `test_camera_preview.c` | **空文件，下一步实现摄像头 YUYV → LCD** |

### 4.6 构建目标

```text
camera_core        # config.c + log.c + lcd.c + yuyv.c
camera             # src/main.c，目前是配置检查占位
test_Get_images    # 采集测试
test_Draw_region   # LCD 区域测试
test_Trans_YUYV    # YUYV 色条演示
config_test        # 配置测试
```

`camera_core` 目前包含 Linux 专用的 `lcd.c`，所以它整体是 Linux-only。
后续若要拆分，可以变成：

```text
camera_common   # config/log/yuyv，可移植
camera_platform # lcd/v4l2，Linux 专用
camera          # main
```

---

## 5. 关键设计决策

1. 语言用 C，不用 C++/Qt；
2. 采集用 V4L2 MMAP，不用 `read()`；
3. 预览目标用 YUYV，拍照用 MJPEG；
4. 显示先用 `/dev/fb0`，不直接上 DRM；
5. 配置用 ini + inih；
6. 日志自己写，不引入 spdlog/zlog；
7. 先单线程，后续再加采集/显示线程和双缓冲；
8. 不提前做 Qt/LVGL/MQTT/RTSP。

---

## 6. 已知技术债

按优先级排序：

1. `test_camera_preview.c` 为空，摄像头帧尚未上屏；
2. `yuyv_to_rgb()` 没有 stride 参数；
3. `yuyv.c` 的全局 `full` 状态，调用顺序隐式依赖；
4. `lcd.c` 全局状态 + 日志生命周期耦合；
5. `include/lcd.h`、`include/yuyv.h` 不是自包含头文件（依赖先包含 `config.h`）；
6. `app_config_t` 过大；
7. 配置路径分散，安装后的 `/etc/...` 尚未接入；
8. `test_Get_images` 对 YUYV 仍使用 `.jpg` 后缀；
9. 没有双缓冲/letterbox/触摸/OSD/网络；
10. 没有单元测试/CI；
11. `scripts/` 当前不可直接执行，等待修正。

---

## 7. 下一步架构演进

短期（v0.1）：

```text
test_camera_preview.c
  -> 打开摄像头
  -> S_FMT YUYV
  -> 读取实际 width/height/bytesperline
  -> 单帧 DQBUF
  -> yuyv_to_rgb(stride)
  -> lcd_put_pixel
  -> QBUF/清理
```

中期（v0.5）：

- 采集循环；
- 双缓冲；
- FPS；
- 把 `v4l2` 采集逻辑从测试程序抽到 `src/camera.c`；
- 把显示循环抽到 `src/display.c`。

长期（v1.0/v2.0）：

- 触摸/OSD；
- letterbox；
- 拍照/存卡；
- 分层 manager；
- Buildroot 集成；
- 单元测试和 CI。

---

## 8. 原则

- 先让画面出来，再谈架构；
- 每次只改一个变量；
- 不为了“看起来专业”提前抽象；
- 但也不要继续把生产代码写进测试文件。
