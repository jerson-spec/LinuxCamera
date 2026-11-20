# LinuxCamera v1.5 发布说明

> 在 v1.0 的本地相机基础上，v1.5 把画面搬到了浏览器里：
> 板子跑一个很小的 HTTP 服务，电脑或手机打开网页就能看 MJPEG 连续流、拍单张。

---

## 能演示什么

- **浏览器看实时画面**：打开 `http://板子IP:8888/`，页面里是 `/stream` 的 MJPEG 流；
- **单张拍照**：`http://板子IP:8888/snapshot.jpg` 返回一张 JPEG；
- **LCD 本地预览**：和 v1.0 一样，触摸左半边拍照、右半边切分辨率、OSD 显示状态；
- **多分辨率**：800×600（15fps）、640×480（30fps）、352×288（30fps）；
- **稳定性**：8 小时本地长跑 + 1 小时网页推流都跑过，RSS/fd 不增长。

## 不能做什么

- **本地 LCD 预览和网页推流同时进行**：摄像头同一时刻只能输出一种格式，推流时 LCD 会暂停；
- **多客户端 / 鉴权 / HTTPS**：现在只支持一个客户端，没有密码；
- **iOS Safari 看流**：Safari 不支持 `multipart/x-mixed-replace`，只能看 `/snapshot.jpg`；
- **缩放**：画面按原分辨率居中，黑边不铺满；
- **网页“拍一张”存卡**：HTTP 拍照只把图发给浏览器，不写 `images/`；存卡用触摸或 `SIGUSR1`；
- **推流期间触摸**：主循环在推流，触摸不响应；退出后可能补触发一次。

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

板子和电脑用 USB 转网口直连（没有路由器也可以）：

```text
Windows USB 网卡 192.168.10.1/24 ──网线── 板子 eth0 192.168.10.50/24
VMware 桥接到 USB 网卡，Ubuntu 192.168.10.2/24（不设网关）
```

板子：

```bash
ip link set eth0 up
ip addr add 192.168.10.50/24 dev eth0
```

部署仍走 ADB（Buildroot 没开 ssh）：

```bash
adb push build-arm/test_camera_preview /root/LinuxCamera/
adb push config/*.ini /root/LinuxCamera/config/

adb shell
cd /root/LinuxCamera
chmod +x test_camera_preview
./test_camera_preview
```

浏览器：

```text
http://192.168.10.50:8888/                首页（实时流 + 拍一张）
http://192.168.10.50:8888/stream          MJPEG 连续流
http://192.168.10.50:8888/snapshot.jpg    单张 JPEG
```

不接屏幕也能触发：

```bash
kill -USR1 <pid>    # 拍照存卡
kill -USR2 <pid>    # 切分辨率
```

## 实测数据

### 延迟（截图法，PC 毫秒时钟 + 相机画面同框）

| 分辨率 | 5 次 | 平均 |
|---|---|---|
| 800×600 | 309 / 340 / 259 / 330 / 319 ms | **311 ms** |
| 640×480 | 270 / 259 / 210 / 309 / 330 ms | **276 ms** |

读数有 ±30–50 ms 误差；延迟大头在相机曝光、USB 和浏览器缓冲。

### 1 小时网页推流（2026-11-20）

| 指标 | 结果 |
|---|---|
| 时长 | 60m19s |
| 预览帧数 | 26856 |
| 推流会话 | 6 次（中间刷新页面） |
| ERROR | 0 |
| dropped | 8（0.03%） |
| RSS | 2652 kB 全程不变 |
| fd | 8（预览）/ 9（推流），无泄漏 |
| CPU | 整体 20%；推流 2%，预览 40% |
| 收尾 | DQbuf = Qbuf = Frames = 26856 |

### 8 小时本地长跑（v1.0，2026-11-14）

610390 帧、47 张照片、15 次分辨率切换、0 错误、RSS/fd 不增长、温度 68–71℃。

## 已知问题

- 暗光下自动曝光会主动降帧（30→25→20），`dropped` 不会跟着涨，不是丢帧；
- 切到 800×600 固定约 1.7 s，因为 USB 带宽要重新协商；
- 触摸驱动上报范围是 800×480，实际坐标是 1024×600，代码里用 `touch_set_range()` 覆盖；
- 推流时 LCD 冻结、触摸不响应，浏览器断开后恢复；
- 运行 10 分钟后 LCD 会被 console blank，`FBIOPAN_DISPLAY` 报错并降级单缓冲；
  演示前 `echo 0 > /sys/class/graphics/fb0/blank`；
- HTTP 单客户端、阻塞式，刷新时浏览器的空连接会占用最多 300ms（已做超时）；
- `lcd_write_line_rgb888()` 只支持 32bpp；
- `yuyv_to_rgb()` 没有 stride 参数，只支持 `bytesperline == width * 2`。

## 代码结构

```text
src/camera.c      V4L2 采集
src/lcd.c         framebuffer、双缓冲、按行写、flip
src/yuyv.c        YUYV → RGB
src/osd.c         8×16 点阵文字
src/touch.c       evdev 单点触摸
src/http.c        HTTP 服务：/、/snapshot.jpg、/stream
src/log.c         分级日志
src/config.c      ini 配置
tests/manual/test_camera_preview.c   v1.5 主程序
scripts/soak_test.sh                 长跑脚本
scripts/mon.sh                       CPU/RSS/fd 采样
scripts/clock.html                   延迟测量的毫秒时钟
```

更详细的内容见 `docs/handoff.md`、`docs/architecture.md`、`docs/performance.md`。

## 许可

MIT，见 `LICENSE.txt`。inih 使用 BSD-3-Clause，见 `third_party/inih/LICENSE.txt`。
