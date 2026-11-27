# 板子与环境

> 本文记录当前开发板、设备节点、网络、工具链、构建、部署和调试方式。
> 如果换了板子、SDK 或摄像头，先更新本文。
>
> 当前阶段：v1.5（网页推流 1 小时通过），W8–W9 驱动基本完成。

---

## 1. 硬件与系统

| 项目 | 当前值 |
|---|---|
| 板子 | 100ask i.MX6ULL |
| CPU 架构 | ARMv7，32-bit，hard-float |
| 系统 | Linux（Buildroot） |
| 摄像头 | USB UVC，当前节点 `/dev/video1` |
| 摄像头请求分辨率 | 1024×600 |
| 摄像头实际输出 | **800×600 YUYV**（驱动调整；640×480 可跑 30fps） |
| 摄像头 `bytesperline` | **1600 = 800 × 2**（640×480 时 1280） |
| LCD | `/dev/fb0`，**1024×600，32bpp** |
| 触摸 | `goodix-ts`，`/dev/input/event1` |
| 按键 | KEY1=GPIO5_IO01→`KEY_F1`；KEY2=GPIO4_IO14→`KEY_F2`，低有效 |
| 背光 | `pwm-backlight`，`/sys/class/backlight/backlight/`，`max_brightness = 8` |
| 光感 | AP3216C，I2C `0x1e`，总线 `21a0000.i2c`（用户态 `/dev/i2c-0`） |
| 显示结果 | letterbox 居中，左右各 112 像素黑色 |
| 主机 | Windows + VMware/Ubuntu 虚拟机 |
| 连接方式 | USB 转网口直连 + 串口（USB-TTL）+ ADB |
| 板子 IP | **192.168.10.50** |
| 交叉工具链 | 100ask Buildroot SDK |

黑边原因：

- 摄像头不支持 1024×600；
- `VIDIOC_S_FMT` 协商后返回 800×600；
- 代码按实际分辨率做 letterbox 居中（W3 起）；
- 铺满需要缩放，属于 v1.0 之后的事。

### 1.1 网络连接

笔记本没有网口，用 USB 转网口和板子直连，不经过路由器：

```text
Windows
  USB 网卡  192.168.10.1/24
      │  网线
      ▼
  板子 eth0 192.168.10.50/24

VMware（Ubuntu）
  VMnet0 桥接到 USB 网卡
  Ubuntu    192.168.10.2/24，不设网关
```

VM 的默认路由仍走 NAT，桥接网卡只负责和板子通信。

板子上临时配置：

```bash
ip link set eth0 up
ip addr add 192.168.10.50/24 dev eth0
ping 192.168.10.1
```

如果 `eth0` 是 `NO-CARRIER`，换 `eth1` 试；用
`cat /sys/class/net/eth0/carrier` 确认哪个口真的有链路。

开机自动配置（Buildroot 没有 ifupdown 时）：

```sh
cat > /etc/init.d/S99network <<'EOF'
#!/bin/sh
case "$1" in
  start)
    ip link set eth0 up
    ip addr add 192.168.10.50/24 dev eth0 2>/dev/null
    ;;
  stop)
    ip addr flush dev eth0
    ;;
esac
EOF
chmod +x /etc/init.d/S99network
```

如果板子上有 `/etc/network/interfaces` 和 `ifup`，也可以写 static stanza。

Windows 侧：USB 网卡手动配 `192.168.10.1/24`，网关和 DNS 留空；
网络位置设成“专用”，否则防火墙会挡 ping。

VMware 侧：虚拟机设置里加一块“桥接”网卡，虚拟网络编辑器把 VMnet0
**手动指定**成 USB 网卡（不要用“自动”，自动经常桥到 Wi-Fi 上）。

验证：

```bash
# 板子
ping 192.168.10.1
ping 192.168.10.2

# Windows
ping 192.168.10.50

# Ubuntu
ping 192.168.10.50
```

直连的副作用：板子关机时 USB 网卡会 `NO-CARRIER`，VM 的桥接网卡跟着断；
这是正常的物理层行为，不是配置错误。

### 1.2 按键与内核模块

两个按键都是低有效：

| 按键 | GPIO | 键码 | 功能 |
|---|---|---|---|
| KEY1 | GPIO5_IO01 | `KEY_F1` | 拍照 |
| KEY2 | GPIO4_IO14 | `KEY_F2` | 切分辨率 |

驱动在 `driver/button_plat/`，设备树补丁 `lc_button.patch`。板子上
`gpio-keys` 必须 `disabled`，否则会抢 GPIO。

编译 / 加载：

```bash
cd driver/button_plat
make
adb push button_plat.ko /root/
adb shell rmmod button_plat 2>/dev/null
adb shell insmod /root/button_plat.ko
adb shell dmesg | tail
```

应该看到两行：

```text
button: 0 irq=... key=59
button: 1 irq=... key=60
```

只有一行说明跑的是旧单键 `.ko`，先 `rmmod` 再 `insmod` 新编的。

板子上没有 `evtest`，看按键事件用：

```bash
dmesg -c >/dev/null
# 按 KEY1/KEY2
dmesg | tail -20
# evbug 会打印 Code/Value
```

或者 `hexdump -C /dev/input/eventX`。

### 1.3 背光 / 光感

**背光**：`pwm-backlight`，`/sys/class/backlight/backlight/`，`max_brightness = 8`。
程序里用百分比对外，内部换算成 0–8 档；80% 落在第 6 档，所以 OSD 上会显示 75%，
这是量化误差不是 bug。手动试：

```bash
B=/sys/class/backlight/backlight
cat $B/max_brightness        # 8
echo 5 > $B/brightness
echo 8 > $B/brightness
```

程序退出时会把启动前的值恢复，`kill -9` 不会。

**光感**：AP3216C，挂在 `21a0000.i2c`（`/dev/i2c-0`）的 `0x1e`。
编号和名字差一位，别被 `i2c1` 这个名字带偏：

```bash
i2cdetect -l                 # 21a0000.i2c 对应 i2c-0
i2cdetect -y 0               # 应看到 0x1e
```

两条读法：

```bash
# 1. 用户态直读（没有驱动时）
i2cset -y 0 0x1e 0x00 0x03
i2cget -y 0 0x1e 0x0c
i2cget -y 0 0x1e 0x0d

# 2. 驱动加载后走 IIO
insmod /root/ap3216c.ko
cat /sys/bus/iio/devices/iio:device*/in_illuminance_input
```

驱动占住地址后 `i2cget` 会报 `Device or resource busy`，正常；`rmmod ap3216c` 恢复。

实测：全黑 0、室内 80–90、手电约 1000。刚上电那几拍读数是 0–3，不可信，
自动亮度做了 1 秒启动宽限。

---

## 2. 设备节点

### 2.1 摄像头

当前使用：

```text
/dev/video1
```

查看：

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video1 --list-formats-ext
v4l2-ctl -d /dev/video1 --get-fmt-video
```

确认 YUYV 支持的分辨率：

```bash
v4l2-ctl -d /dev/video1 --list-formats-ext | less
```

如果列表中没有 1024×600，驱动返回 800×600 是正常的。

单独测试：

```bash
v4l2-ctl -d /dev/video1 \
  --set-fmt-video=width=1024,height=600,pixelformat=YUYV
v4l2-ctl -d /dev/video1 --get-fmt-video
```

### 2.2 LCD

当前使用：

```text
/dev/fb0
```

查看：

```bash
fbset -i
cat /sys/class/graphics/fb0/bits_per_pixel
cat /sys/class/graphics/fb0/stride
cat /sys/class/graphics/fb0/virtual_size
```

当前 `lcd_put_pixel()` 支持：

- 16bpp：RGB565；
- 32bpp：按 `0x00RRGGBB` 写入。

如果颜色不对，先检查 `red/green/blue offset` 和 RGB/BGR。

当前实测（2026-11-08）：

- `bits_per_pixel = 32`，`line_length = 4096`；
- 可见区 1024×600，一屏约 2.4 MB（1024×600×4）；
- `smem_len = 32 MB`，是 BSP 预留的整块显存，**不等于**两屏；
  `yres_virtual` 只描述“这块内存怎么用”，不会重新分配；
- 程序会把 `yres_virtual` 设为 1200，并用 `FBIOPAN_DISPLAY` 切前后台。

注意：`echo 1 > /sys/class/graphics/fb0/blank` 之后，`FBIOPAN_DISPLAY` 会失败并返回
`-EPERM`（内核 `fb_pan_display()` 在非 RUNNING 状态直接拒绝），日志里会出现交替的
`yoffset: 0 / 600` 错误——这是**预期现象**，也正好证明 flip 在每帧切换。
`echo 0 > blank` 恢复后，建议重启程序。

### 2.3 输入设备

当前板子上通常有：

| 节点 | 设备 | 用途 |
|---|---|---|
| `/dev/input/event0` | `snvs-powerkey` | 电源键，不用 |
| `/dev/input/event1` | `goodix-ts` | 触摸屏 |
| `/dev/input/event2` | USB 2.0 Camera | 摄像头自带按键（`KEY_CAMERA`），不用 |
| `/dev/input/event3` | `lc-button` | 我们的按键驱动，`insmod` 后才出现 |

`event` 编号会随加载顺序变化，故应用里**没有写死**，用
`/sys/class/input/event*/device/name` 按名字找。触摸找 `goodix-ts`，按键找
`lc-button`。

---

## 3. 交叉工具链

`cmake/toolchain-arm.cmake` 当前：

```cmake
set(SDK_ROOT "/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot")

set(CMAKE_C_COMPILER
    "${SDK_ROOT}/bin/arm-buildroot-linux-gnueabihf-gcc"
)

set(CMAKE_SYSROOT
    "${SDK_ROOT}/arm-buildroot-linux-gnueabihf/sysroot"
)
```

确认：

```bash
ls -l "$SDK_ROOT/bin/arm-buildroot-linux-gnueabihf-gcc"
"$SDK_ROOT/bin/arm-buildroot-linux-gnueabihf-gcc" --version
"$SDK_ROOT/bin/arm-buildroot-linux-gnueabihf-gcc" -dumpmachine
```

预期：

```text
arm-buildroot-linux-gnueabihf
```

---

## 4. 构建

### 4.1 RelWithDebInfo

```bash
cd ~/Desktop/LinuxCamera

cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-arm -j"$(nproc)"
```

### 4.2 Debug

```bash
cmake -S . -B build-arm-dbg \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build-arm-dbg -j"$(nproc)"
```

### 4.3 单目标

```bash
cmake --build build-arm --target test_camera_preview -j"$(nproc)"
cmake --build build-arm --target test_Trans_YUYV -j"$(nproc)"
```

### 4.4 检查

```bash
file build-arm/test_camera_preview
grep CMAKE_C_COMPILER build-arm/CMakeCache.txt
```

如果 RelWithDebInfo 出现配置打印异常，先彻底删除 `build-arm` 重编，排除旧对象/ABI 问题。

---

## 5. 部署

板子现在有 IP `192.168.10.50`，但 Buildroot 没开 ssh，部署仍走 ADB。
（以后开了 ssh/scp 可以直接走网络。）

```bash
adb devices
adb shell mkdir -p /root/LinuxCamera/config

cd ~/Desktop/LinuxCamera

adb push build-arm/test_Get_images /root/LinuxCamera/
adb push build-arm/test_Draw_region /root/LinuxCamera/
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/
adb push build-arm/test_camera_preview /root/LinuxCamera/
adb push scripts/soak_test.sh /root/LinuxCamera/
adb push scripts/mon.sh /root/LinuxCamera/

adb push config/camera.ini /root/LinuxCamera/config/
adb push config/lcd.ini /root/LinuxCamera/config/
adb push config/trans.ini /root/LinuxCamera/config/

adb shell
cd /root/LinuxCamera
chmod +x test_Get_images test_Draw_region test_Trans_YUYV test_camera_preview soak_test.sh mon.sh
```

程序跑起来后，网络这边可以直接测：

```bash
curl -v http://192.168.10.50:8888/
curl -v http://192.168.10.50:8888/snapshot.jpg -o test.jpg
```

Debug 版：

```bash
adb push build-arm-dbg/test_camera_preview /root/LinuxCamera/test_camera_preview_dbg
```

---

## 6. 运行

### 6.1 配置检查

```bash
./camera config/camera.ini
./config_test config/camera.ini
```

### 6.2 采集/保存

```bash
./test_Get_images
```

当前 `camera.ini` 可能为 YUYV；如果要保存 JPEG，先改成 `format=MJPG`。  
`test_Get_images` 对 YUYV 仍使用 `.jpg` 后缀，这是已知问题。

### 6.3 LCD 区域

```bash
./test_Draw_region
```

### 6.4 YUYV 色条

```bash
./test_Trans_YUYV
```

### 6.5 摄像头预览 + 触摸（v1.5）

```bash
./test_camera_preview
```

默认 `camera.ini` 请求 1024×600，摄像头实际给 800×600，画面居中：

- 左上角 OSD：`res: 800*600`、`fps: ...`、`drop: ...`、`tap left: shot / tap right: res`；
- 点屏幕**左半边**：拍一张 jpg 到 `images/`；
- 点**右半边**：在 800×600 / 640×480 / 352×288 之间轮换；
- 每秒打印一行：
  `fps: 15.0, wait: 43.9, conv: 14.0, draw: 8.6, dropped: 0, rss: 6064 kB`；
- 按 Ctrl-C 干净退出，收尾打印 `DQbuf = Qbuf = Frames`。

不接屏幕也能测：

```bash
kill -USR1 <pid>   # 拍照
kill -USR2 <pid>   # 切分辨率
```

触摸坐标不对时，先 `evtest /dev/input/event1` 摸四角，再把实测范围填到
`test_camera_preview.c` 的 `touch_set_range()`（这颗屏驱动报 800×480，实际是 1024×600）。

### 6.6 长跑测试

```bash
nohup ./test_camera_preview >/dev/null 2>&1 &
nohup ./soak_test.sh 8 >/dev/null 2>&1 &     # 8 小时；传 1 就是 1 小时
```

脚本每 10 分钟拍一张、每 30 分钟切一次分辨率、每 60 秒记录
RSS/fd/温度/剩余空间/图片数，日志在 `soak/` 下。

8 小时实测（2026-11-14）：

- 610390 帧、47 张照片、15 次切换；
- `errors=0 warnings=0 poll_timeouts=0`；
- `VmRSS` 在 1472–6548 kB 之间摆，`VmHWM 7104 kB`，无上升趋势；
- `fd=7` 全程不变；温度 68.1–70.6 ℃；
- 收尾 `DQbuf = Qbuf = Frames = 610390`。

### 6.7 浏览器访问

板子和电脑/手机在同一网络后：

```text
http://192.168.10.50:8888/                首页
http://192.168.10.50:8888/stream          MJPEG 流
http://192.168.10.50:8888/snapshot.jpg    单张
```

- 推流时摄像头切 MJPG，本地 LCD 预览暂停，断开后自动恢复；
- 网页“拍一张”只回图，不写 `images/`；
- iOS Safari 不支持 MJPEG 流；
- 实测延迟：800×600 约 311 ms、640×480 约 276 ms；
- 1 小时推流：26856 帧、dropped 8、RSS/fd 不增长。

---

## 7. GDB

板子：

```bash
cd /root/LinuxCamera
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

常用断点/变量：

```gdb
b test_camera_preview.c:176
b test_camera_preview.c:294
b yuyv_to_rgb
b lcd_put_pixel

p camera_cfg
p vfmt.fmt.pix
p buf.bytesused
p mem_addr
p lcd_var_screeninfo_t
```

注意：

- 远程调试不用 `r`，用 `c`；
- 程序退出后 gdbserver 结束；
- gdb 仍走 ADB 转发；板子现在有 `192.168.10.50`，HTTP 直接访问；
- `scripts/gdb.sh` 目前不完整，先使用上面的命令。

---

## 8. 常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| `Exec format error` | 推了 x86 程序 | 推 `build-arm/` 下 ARM 程序 |
| `GLIBC_2.xx not found` | 工具链/rootfs 不匹配 | 用 100ask SDK，或静态链接 |
| `config: cannot open ...` | 当前目录不对/配置没推 | `cd /root/LinuxCamera`，推 `config/` |
| `parse log level error` | 配置字段被截断/CRLF/旧对象 | `od -c config/camera.ini`，清 build 重编 |
| `open /dev/video1 error` | 节点不对/权限 | `ls -l /dev/video*` |
| `open /dev/fb0 error` | 节点不对/权限 | `ls -l /dev/fb*` |
| 画面斜/花 | `bytesperline`/stride 不匹配 | 打印 `bytesperline` |
| 画面绿/雪花 | 把 MJPEG 当 YUYV | 检查 `S_FMT` 实际格式 |
| 画面有黑边 | 摄像头分辨率 < LCD 分辨率 | letterbox 居中，正常 |
| 帧率突然掉到 25/20/15 | 暗光自动曝光拉长曝光时间 | `v4l2-ctl --get-parm` 确认；不是丢帧 |
| `FBIOPAN_DISPLAY` 报错 | fb 被 blank，或驱动不支持 pan | `cat /sys/class/graphics/fb0/blank`；`echo 0 > blank` 或让程序降级 |
| `camera.log` 是空的 | `lcd_init()` 把日志重定向到了 `lcd.log` | 看 `lcd.log` |
| 程序跑完 gdb 断开 | 程序正常退出 | 重启 gdbserver |

---

## 9. 当前限制

- 板子有静态 IP `192.168.10.50`，但 gdb 仍走 ADB；
- 摄像头 800×600@15fps / 640×480@30fps，LCD 1024×600，letterbox 居中；
- 暗光下自动曝光会主动降帧，不是丢帧；
- 触摸驱动报的范围是 800×480，实际是 1024×600，代码里已覆盖；
- `lcd_write_line_rgb888()` 只支持 32bpp；
- 推流时本地预览暂停；网页“拍一张”不落盘；iOS Safari 不支持 MJPEG 流；
- 没有缩放、没有多客户端/鉴权/HTTPS；
- 按键驱动假设 `button-gpios` 正好两个 GPIO；`gpio-keys` 必须 disabled；
- 改完内核模块必须 `rmmod` 再 `insmod`，否则跑旧 `.ko`；
- 板子上没有 `evtest`，用 `dmesg` 的 evbug 或 `hexdump`；
- 主循环里 `poll revents mismatch` 的 WARN 已经注释掉，不用再管；
- 光感走驱动时 IIO 设备号不固定，应用按 `name` 找；驱动加载后 `i2cget` 报 EBUSY 属正常；
- 自动亮度上电后有 1 秒宽限，手动滑动后 30 秒内不会被自动改回去；
- 切到 800×600 固定 1.7 s，首帧延迟 1–1.6 s；
- 温度长跑稳定在 68–71℃。

  
