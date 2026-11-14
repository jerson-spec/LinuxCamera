# LinuxCamera v1.0 发布说明

> i.MX6ULL + Linux V4L2 相机项目的第一个可用版本。
> 这个版本的重点不是“功能多”，而是**一条完整的数据通路跑通了，并且连续跑了 8 小时**。

---

## 能演示什么

- **实时预览**：摄像头 YUYV → RGB（定点）→ framebuffer，双缓冲翻页；
- **分辨率**：800×600（15fps）、640×480（30fps）、352×288（30fps），运行时切换，不重启；
- **letterbox**：画面按屏幕居中，不再贴左上角；
- **触摸**：点屏幕左半边拍照，点右半边切分辨率；
- **拍照**：切到 MJPEG 取一帧，原样写 `.jpg`，不做软件编码；
- **OSD**：左上角显示分辨率、帧率、丢帧数；
- **长跑**：8 小时、610390 帧、0 错误、内存和 fd 不增长。

## 不能做什么（有意留到后面）

- 缩放（放大/缩小）；
- 网络推流（v1.5 的 MJPEG over HTTP）；
- 开机自启、看门狗、Buildroot 精简镜像（v2.0）；
- 多摄像头、DRM、硬件编解码。

## 构建

Ubuntu + 100ask Buildroot SDK：

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"
file build-arm/test_camera_preview     # 应该是 ARM
```

## 部署与运行

```bash
adb push build-arm/test_camera_preview /root/LinuxCamera/
adb push config/*.ini /root/LinuxCamera/config/
adb push scripts/soak_test.sh /root/LinuxCamera/

adb shell
cd /root/LinuxCamera
chmod +x test_camera_preview soak_test.sh
./test_camera_preview
```

不接屏幕也能触发：

```bash
kill -USR1 <pid>    # 拍照
kill -USR2 <pid>    # 切分辨率
```

## 实测数据（2026-11-13 19:38 → 11-14 03:38）

| 指标 | 结果 |
|---|---|
| 时长 | 8 h 00 m |
| 帧数 | 610390 |
| 拍照 | 47 张（每 10 分钟一张） |
| 分辨率切换 | 15 次 |
| ERROR / WARN / poll timeout（长跑期间） | 0 / 0 / 0 |
| VmRSS | 1472–6548 kB 摆动，VmHWM 7104 kB，无上升趋势 |
| fd | 7，全程不变 |
| 温度 | 68.1–70.6 ℃ |
| dropped | 41 帧（0.0067%） |
| 收尾 | DQbuf = Qbuf = Frames = 610390 |

单帧耗时（800×600）：conv 14.0 ms + draw 8.6 ms，CPU 忙占比约 34%。

## 已知问题

- 暗光下自动曝光会主动降帧（30→25→20），`dropped` 不变，不是丢帧；
- 切到 800×600 固定约 1.7 s，因为 USB 带宽要重新协商；
- 触摸驱动上报范围是 800×480，实际坐标是 1024×600，代码里用 `touch_set_range()` 覆盖；
- `lcd_write_line_rgb888()` 只支持 32bpp；
- `yuyv_to_rgb()` 没有 stride 参数，只支持 `bytesperline == width * 2`；
- 开机首帧延迟 1–1.6 s。

## 代码结构

```text
src/camera.c      V4L2 采集（open / set_format / start / stop / dequeue / queue）
src/lcd.c         framebuffer、双缓冲、按行写、flip
src/yuyv.c        YUYV ↔ RGB
src/osd.c         8×16 点阵文字
src/touch.c       evdev 单点触摸
src/log.c         分级日志
src/config.c      ini 配置
tests/manual/test_camera_preview.c   v1.0 主程序
scripts/soak_test.sh                 长跑脚本
```

更详细的内容见 `docs/handoff.md`、`docs/architecture.md`、`docs/performance.md`。

## 许可

MIT，见 `LICENSE.txt`。inih 使用 BSD-3-Clause，见 `third_party/inih/LICENSE.txt`。
