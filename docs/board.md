# 板子与环境

> 本文记录当前开发板、设备节点、工具链、构建、部署和调试方式。
> 如果换了板子、SDK 或摄像头，先更新本文。
>
> 当前阶段：W3 完成（预览优化 + 多分辨率 + letterbox）。

---

## 1. 硬件与系统

| 项目 | 当前值 |
|---|---|
| 板子 | 100ask i.MX6ULL |
| CPU 架构 | ARMv7，32-bit，hard-float |
| 系统 | Linux |
| 摄像头 | USB UVC，当前节点 `/dev/video1` |
| 摄像头请求分辨率 | 1024×600 |
| 摄像头实际输出 | **800×600 YUYV**（驱动调整；640×480 可跑 30fps） |
| 摄像头 `bytesperline` | **1600 = 800 × 2**（640×480 时 1280） |
| LCD | `/dev/fb0`，**1024×600，32bpp** |
| 显示结果 | letterbox 居中，左右各 112 像素黑色 |
| 主机 | Ubuntu 虚拟机 |
| 连接方式 | ADB（USB），当前未配置板子 IP |
| 交叉工具链 | 100ask Buildroot SDK |

黑边原因：

- 摄像头不支持 1024×600；
- `VIDIOC_S_FMT` 协商后返回 800×600；
- 代码按实际分辨率做 letterbox 居中（W3 起）；
- 铺满需要缩放，属于 v1.0 之后的事。

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

当前板子通过 ADB 连接，没有配置网络 IP。

```bash
adb devices
adb shell mkdir -p /root/LinuxCamera/config

cd ~/Desktop/LinuxCamera

adb push build-arm/test_Get_images /root/LinuxCamera/
adb push build-arm/test_Draw_region /root/LinuxCamera/
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/
adb push build-arm/test_camera_preview /root/LinuxCamera/

adb push config/camera.ini /root/LinuxCamera/config/
adb push config/lcd.ini /root/LinuxCamera/config/
adb push config/trans.ini /root/LinuxCamera/config/

adb shell
cd /root/LinuxCamera
chmod +x test_Get_images test_Draw_region test_Trans_YUYV test_camera_preview
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

### 6.5 摄像头连续预览

```bash
./test_camera_preview
```

默认 `camera.ini` 请求 1024×600，摄像头实际给 800×600，画面居中：

- 左右各 112 像素黑边，上下贴边；
- 每秒打印一行：
  `fps: 15.0, wait: 43.9, conv: 14.0, draw: 8.6, dropped: 0, rss: 6064 kB`；
- 按 Ctrl-C 干净退出，收尾打印 `DQbuf = Qbuf = Frames`。

想看 30fps，把 `config/camera.ini` 改成 `width=640 / height=480` 再跑。
程序会用 `VIDIOC_S_PARM` 请求 30fps；摄像头支持就给 30，不支持会自己降。
暗光下自动曝光也会把帧率降到 25/20/15——判断依据是 `wait` 变大而 `dropped` 不变。
这时用 `v4l2-ctl -d /dev/video1 --get-parm` 能看到当前帧率已经变了。

### 6.6 10 分钟稳定性测试

```bash
# 终端 A
./test_camera_preview

# 终端 B：每 60 秒记一次内存
while true; do
    grep VmRSS /proc/$(pidof test_camera_preview)/status
    sleep 60
done
```

通过标准（2026-11-08 实测）：

- 604 s / 9036 帧 / 平均 14.96 fps；
- `dropped` 不随时间增长（本轮为 0）；
- `VmRSS` 6324 kB 恒定；
- 退出时 `DQbuf == Qbuf == Frames`。

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
- 当前板子没有 IP，通过 ADB 转发；
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

- 板子没有 IP，gdb 走 ADB；
- 摄像头 800×600@15fps / 640×480@30fps，LCD 1024×600，letterbox 居中；
- 暗光下自动曝光会主动降帧，不是丢帧；
- `lcd_write_line_rgb888()` 只支持 32bpp；
- 没有触摸/OSD/网络；拍照还没做；
- 首帧延迟 1–1.6 s（摄像头启动 + 曝光收敛）；

  
