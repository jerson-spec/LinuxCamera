# 板子与环境

> 本文记录当前开发板、设备节点、工具链、构建、部署和调试方式。
> 如果换了板子、SDK 或摄像头，先更新本文。

---

## 1. 硬件与系统

| 项目 | 当前值 |
|---|---|
| 板子 | 100ask i.MX6ULL |
| CPU 架构 | ARMv7，32-bit，hard-float |
| 系统 | Linux |
| 摄像头 | USB UVC，当前节点 `/dev/video1` |
| 屏幕 | LCD，framebuffer `/dev/fb0` |
| 主机 | Ubuntu 虚拟机 |
| 连接方式 | ADB（USB），当前未配置板子 IP |
| 交叉工具链 | 100ask Buildroot SDK |

板子上确认：

```bash
uname -a
uname -m
# 预期：armv7l

ls -l /dev/video*
ls -l /dev/fb*
```

---

## 2. 设备节点

### 2.1 摄像头

当前使用：

```text
/dev/video1
```

查看支持格式：

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video1 --list-formats-ext
v4l2-ctl -d /dev/video1 --get-fmt-video
```

注意：

- 不是所有 `/dev/videoX` 都是采集节点，有些是 metadata 节点；
- 以 `--list-devices` 和 `--list-formats-ext` 为准；
- 当前代码默认 `device=/dev/video1`，在 `config/camera.ini` 修改。

### 2.2 LCD

当前使用：

```text
/dev/fb0
```

查看参数：

```bash
fbset -i
cat /sys/class/graphics/fb0/bits_per_pixel
cat /sys/class/graphics/fb0/stride
cat /sys/class/graphics/fb0/virtual_size
```

代码运行时会打印：

- `xres / yres`；
- `xres_virtual / yres_virtual`；
- `bits_per_pixel`；
- `line_length`；
- `red/green/blue offset`。

当前 `lcd_put_pixel()` 支持：

- 16bpp：RGB565；
- 32bpp：按 `0x00RRGGBB` 写入。

如果屏幕是 BGR 或特殊 32bpp 格式，颜色会不对，需要根据 `red/green/blue offset` 调整。

---

## 3. 交叉工具链

当前 `cmake/toolchain-arm.cmake`：

```cmake
set(SDK_ROOT "/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot")

set(CMAKE_C_COMPILER
    "${SDK_ROOT}/bin/arm-buildroot-linux-gnueabihf-gcc"
)

set(CMAKE_SYSROOT
    "${SDK_ROOT}/arm-buildroot-linux-gnueabihf/sysroot"
)
```

确认编译器：

```bash
ls -l "$SDK_ROOT/bin/arm-buildroot-linux-gnueabihf-gcc"
"$SDK_ROOT/bin/arm-buildroot-linux-gnueabihf-gcc" --version
"$SDK_ROOT/bin/arm-buildroot-linux-gnueabihf-gcc" -dumpmachine
```

预期：

```text
arm-buildroot-linux-gnueabihf
```

如果 SDK 路径不同，修改 `toolchain-arm.cmake` 的 `SDK_ROOT`。

---

## 4. 构建

### 4.1 Release / RelWithDebInfo

```bash
cd ~/Desktop/LinuxCamera

cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-arm -j"$(nproc)"
```

### 4.2 Debug（gdb 用）

```bash
cmake -S . -B build-arm-dbg \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build-arm-dbg -j"$(nproc)"
```

### 4.3 检查产物

```bash
file build-arm/test_Get_images
file build-arm/test_Trans_YUYV
file build-arm-dbg/test_Trans_YUYV
```

预期：

```text
ELF 32-bit LSB executable, ARM, EABI5 ...
```

如果是 x86-64，说明工具链没生效，检查：

```bash
grep CMAKE_C_COMPILER build-arm/CMakeCache.txt
```

---

## 5. 部署

当前板子通过 ADB 连接，没有配置网络 IP。

### 5.1 确认 ADB

```bash
adb devices
```

设备应处于 `device` 状态。

### 5.2 创建目录

```bash
adb shell mkdir -p /root/LinuxCamera/config
```

### 5.3 推送程序和配置

```bash
cd ~/Desktop/LinuxCamera

adb push build-arm/test_Get_images /root/LinuxCamera/
adb push build-arm/test_Draw_region /root/LinuxCamera/
adb push build-arm/test_Trans_YUYV /root/LinuxCamera/

adb push config/camera.ini /root/LinuxCamera/config/
adb push config/lcd.ini /root/LinuxCamera/config/
adb push config/trans.ini /root/LinuxCamera/config/
```

Debug 版：

```bash
adb push build-arm-dbg/test_Trans_YUYV /root/LinuxCamera/test_Trans_YUYV_dbg
```

### 5.4 板子上执行

```bash
adb shell
cd /root/LinuxCamera
chmod +x test_Get_images test_Draw_region test_Trans_YUYV
```

---

## 6. 运行

### 6.1 配置检查

```bash
./camera config/camera.ini
./config_test config/camera.ini
```

### 6.2 采集

先枚举：

```ini
# camera.ini
flag=1
```

```bash
./test_Get_images
```

再采集：

```ini
flag=0
save_count=20
```

```bash
./test_Get_images
```

### 6.3 LCD 色条

```bash
./test_Trans_YUYV
```

默认读：

```text
config/trans.ini
config/lcd.ini
```

当前 `trans.ini` 的 `trans_display_time` 可能较长，测试时改成 5 秒。

### 6.4 LCD 区域测试

```bash
./test_Draw_region
```

默认读 `config/lcd.ini`。

---

## 7. GDB 调试

板子端：

```bash
cd /root/LinuxCamera
gdbserver :1234 ./test_Trans_YUYV
```

Ubuntu 端：

```bash
adb forward tcp:1234 tcp:1234

GDB=$(find "$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot" \
      -type f -name '*-gdb' | head -n 1)

echo "$GDB"

"$GDB" build-arm-dbg/test_Trans_YUYV
```

gdb 里：

```gdb
set pagination off
set confirm off
target remote 127.0.0.1:1234
b main
c
```

常用断点：

```gdb
b lcd_init
b lcd_put_pixel
b yuyv_to_rgb
b generate_yuyv_region
```

常用变量：

```gdb
p mem_addr
p lcd_fix_screeninfo_t
p lcd_var_screeninfo_t
p cfg
p full
```

退出：

```gdb
detach
quit
```

清理转发：

```bash
adb forward --remove-all
```

若板子有 IP，也可以不走 ADB：

```gdb
target remote <board-ip>:1234
```

当前项目暂未配置板子 IP，优先使用 ADB。

---

## 8. 常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| `Exec format error` | 推了 x86 程序 | 推 `build-arm/` 下的 ARM 程序 |
| `GLIBC_2.xx not found` | 工具链和板子 rootfs 不匹配 | 使用 100ask Buildroot SDK，或静态链接 |
| `config: cannot open ...` | 当前目录不对/配置没推 | `cd /root/LinuxCamera`，推 `config/` |
| `open /dev/video1 error` | 设备节点不对/权限 | `ls -l /dev/video*` |
| `open /dev/fb0 error` | fb 节点不对/权限 | `ls -l /dev/fb*` |
| 画面花/斜 | `line_length`/bpp/分辨率不匹配 | 打印 fix/var 参数 |
| 颜色不对 | RGB/BGR、16/32bpp、YUV 范围 | 先画纯红/绿/蓝 |
| gdbserver 连不上 | 未 `adb forward`/gdbserver 未启动 | 检查端口和进程 |
| 程序跑完 gdb 断开 | 程序正常退出 | 重新启动 gdbserver |

---

## 9. 当前限制

- 板子还没有配置网络 IP，gdb 通过 ADB 转发；
- 摄像头 YUYV 帧还没有直接显示到 LCD；
- `test_camera_preview.c` 目前为空；
- LCD 分辨率、摄像头分辨率需要实际用 `fbset` / `v4l2-ctl` 确认；
- 当前没有双缓冲、letterbox、触摸、OSD；
- `scripts/` 目前还不能直接执行，命令以本文为准。
