# 项目交接（Handoff）

> **新会话先读本文，再按下面的顺序补细节。**
>
> 推荐顺序：
> 1. 本文（项目是什么、做到哪、怎么跑、下一步）
> 2. `docs/driver.md`（W8–W9 驱动细节：hello_misc、button_plat、ap3216c）
> 3. `docs/architecture.md`（模块、数据流、技术债）
> 4. `docs/board.md`（板子、网络、部署、调试）
> 5. `docs/roadmap.md`（版本计划、memo 里的产品想法）
> 6. `docs/performance.md`（8 小时、1 小时推流、延迟数据）
> 7. `README.md`（对外入口）
>
> `docs/debug-log.md` 是用户自己维护的踩坑记录，**不要改它**。

---

## 0. 快速事实

| 项目 | 当前值 |
|---|---|
| 项目 | i.MX6ULL Linux 相机（本地 + 网页） |
| 当前阶段 | **v1.5 已打标签；W8–W9 驱动基本完成** |
| 应用层 | v1.5：预览 + 触摸 + OSD + 拍照 + HTTP 推流；背光/自动亮度已接 |
| 驱动层 | 已完成 `hello_misc`、`button_plat`、`ap3216c`（IIO 光感） |
| 语言 | C（用户态），C（内核模块） |
| 采集 | V4L2 MMAP，4 buffer，`poll`，`VIDIOC_S_PARM` |
| 显示 | `/dev/fb0`，1024×600，32bpp，双缓冲 + letterbox |
| 摄像头 | `/dev/video1`，800×600@15 / 640×480@30 / 352×288@30 |
| 触摸 | `goodix-ts`，`/dev/input/event1`（按名字找，不写死） |
| 按键 | KEY1=GPIO5_1→`KEY_F1`拍照；KEY2=GPIO4_14→`KEY_F2`切分辨率 |
| 背光 | `/sys/class/backlight/backlight/`，`max_brightness = 8` |
| 光感 | AP3216C，I2C `0x1e`，总线 `21a0000.i2c`（`/dev/i2c-0`） |
| 板子 IP | `192.168.10.50`（USB 转网口直连 PC） |
| HTTP | `/`、`/snapshot.jpg`、`/stream`，端口 8888 |
| 当前标签 | `v1.0`、`v1.5`，以及几个里程碑标签；HEAD 见 `git log --oneline -3` |
| 下一步 | 锁定触摸屏、OSD 设置菜单，然后 v2.0 |

一句话：

> **v1.5 已经能演示：LCD 预览、触摸/按键拍照、切分辨率，浏览器看 MJPEG 流；
> 现在按键、背光、光感都通过自写驱动接进来了，屏幕会跟着环境光调亮度。**

---

## 1. 这个项目是什么

### 1.1 目标

用户是嵌入式 Linux 学习者，这个项目是**主项目**，目标不只是“学 API”，而是：

> 一个能演示、能讲清楚、坏了能自己修的嵌入式相机终端。
> 本地能看能拍能设置，远程浏览器能看能管理，长期能稳定跑。

产品定位和 memo 里的想法见 `docs/roadmap.md` 第 10 节，简单说：

- 本地：预览、触摸、按键、光感、OSD 设置；
- 远程：`/`、`/snapshot.jpg`、`/stream`；
- 长期：开机自启、看门狗、Buildroot 精简。

### 1.2 用户的工作方式（很重要）

- **核心数据通路是用户自己写的**（V4L2、转换、framebuffer、HTTP 主体）；
- 希望 AI 做 **review、解释原因、给骨架、规划**，不要一上来重写整个模块；
- **每次只改一个变量，上板验证，再继续**；
- 文档口吻要像人写的，不要 AI 腔，尤其是 `performance.md` 和 `README.md`；
- **没让改就不要改**；要改先说明原因和范围；
- `docs/debug-log.md` 是用户自己的，不要动；
- 用户会自己编译、上板、贴日志，AI 负责定位问题和给方案。

### 1.3 版本时间线

```text
v0.1   单帧 YUYV → RGB → LCD
v0.5   连续预览 + 双缓冲 + FPS，10 分钟不崩
W3     YUYV→RGB 定点化 + 按行写，CPU 98%→34%，640×480@30
v1.0   触摸 + OSD + MJPEG 拍照 + 8 小时长跑（610390 帧，0 错误）
v1.5   HTTP：/、/snapshot.jpg、/stream；1 小时推流，延迟约 300ms
W8–W9  hello_misc → button_plat（platform + input）→ ap3216c（IIO 光感）→ 自动亮度
```

---

## 2. 仓库结构

```text
LinuxCamera/
├── CMakeLists.txt
├── README.md
├── LICENSE.txt
├── cmake/toolchain-arm.cmake
├── config/
│   ├── camera.ini
│   ├── lcd.ini
│   └── trans.ini
├── include/
│   ├── autolight.h
│   ├── backlight.h
│   ├── camera.h
│   ├── config.h
│   ├── http.h
│   ├── lcd.h
│   ├── log.h
│   ├── lux.h
│   ├── osd.h
│   ├── snapshot.h
│   ├── touch.h
│   └── yuyv.h
├── src/
│   ├── autolight.c
│   ├── backlight.c
│   ├── camera.c
│   ├── config.c
│   ├── http.c
│   ├── lcd.c
│   ├── log.c
│   ├── lux.c
│   ├── osd.c
│   ├── touch.c
│   ├── yuyv.c
│   └── main.c
├── driver/                    # 内核模块，不参与 CMake
│   ├── README.md
│   ├── hello_misc/
│   │   ├── hello_misc.c
│   │   ├── Makefile
│   │   ├── README.md
│   │   └── test_hello.c
│   ├── button_plat/
│   │   ├── button_plat.c
│   │   ├── Makefile
│   │   └── lc_button.patch    # 设备树补丁
│   └── ap3216c/
│       ├── ap3216c.c
│       ├── Makefile
│       ├── ap3216c.patch      # 内核树 .dts 差异（含之前 lc_button 改动）
│       └── README.md
├── tests/
│   ├── manual/
│   │   ├── test_Get_images.c
│   │   ├── test_Draw_region.c
│   │   ├── test_Trans_YUYV.c
│   │   └── test_camera_preview.c
│   └── unit/config_test.c
├── third_party/inih/
├── docs/
│   ├── architecture.md
│   ├── board.md
│   ├── driver.md
│   ├── handoff.md
│   ├── performance.md
│   ├── release-v1.0.md
│   ├── release-v1.5.md
│   ├── roadmap.md
│   ├── debug-log.md
│   └── pic/
├── scripts/
│   ├── build.sh
│   ├── build_dbg.sh
│   ├── deploy.sh
│   ├── deploy_dbg.sh
│   ├── gdb.sh
│   ├── setup_env.sh
│   ├── soak_test.sh
│   ├── mon.sh
│   └── clock.html
├── build-arm/
└── stage/
```

---

## 3. 硬件与环境

### 3.1 板子和外设

- 100ask i.MX6ULL，ARMv7 32-bit hard-float；
- 摄像头：USB UVC `/dev/video1`，请求 1024×600，实际 800×600；
- LCD：`/dev/fb0`，1024×600，32bpp，`line_length=4096`，`smem_len=32MB`；
- 触摸：`goodix-ts`，`/dev/input/event1`；
- 按键：
  - KEY1 = GPIO5_IO01（SNVS_TAMPER1），低有效，`KEY_F1`；
  - KEY2 = GPIO4_IO14（NAND_CE1_B），低有效，`KEY_F2`；
- 背光：DTS 里有 `pwm-backlight`，`/sys/class/backlight/backlight/`，
  `max_brightness = 8`；
- 光感：AP3216C，I2C `0x1e`，总线 `21a0000.i2c`（用户态 `/dev/i2c-0`），
  已确认并有驱动；
- 串口：USB-TTL，115200 8N1；
- 网络：USB 转网口直连 PC。

### 3.2 网络

```text
Windows USB 网卡 192.168.10.1/24 ──网线── 板子 eth0 192.168.10.50/24
VMware VMnet0 桥接到 USB 网卡，Ubuntu 192.168.10.2/24（不设网关）
```

板子临时配置：

```bash
ip link set eth0 up
ip addr add 192.168.10.50/24 dev eth0
```

持久化：Buildroot 没 ifupdown 时写 `/etc/init.d/S99network`；有 ifup 时写
`/etc/network/interfaces`。细节见 `docs/board.md`。

直连的副作用：板子关机时 USB 网卡 `NO-CARRIER`，VM 桥接网卡跟着断，正常。

### 3.3 工具链

- 100ask Buildroot SDK；
- 应用：`cmake/toolchain-arm.cmake`，CC 指向
  `.../ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/...`；
- 内核模块：用板子对应的 `Linux-4.9.88` 内核树编译，vermagic 必须一致。

---

## 4. 软件架构

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
    src/http.c
)
```

Linux-only。

### 4.2 主要模块

| 模块 | 作用 |
|---|---|
| `camera.c` | V4L2：open / set_format / set_fps / start / dequeue / queue / stop |
| `lcd.c` | framebuffer、双缓冲、`lcd_write_line_rgb888`、`lcd_flip` |
| `yuyv.c` | YUYV ↔ RGB，定点；`full` 是全局，先 `Is_full_range()` |
| `osd.c` | 8×16 点阵，`osd_show_text` / 阴影 / 右对齐 / 矩形 |
| `touch.c` | evdev 单点；`touch_read` 返回 -1/0/1；`touch_set_range` 覆盖坐标范围 |
| `http.c` | `/`、`/snapshot.jpg`、`/stream`；单客户端；300ms 空连接超时 |
| `config.c` | inih，`app_config_t` 偏大，未拆 |
| `log.c` | DEBUG/INFO/WARN/ERROR，无轮转 |

### 4.3 `test_camera_preview.c`

这是 v1.5 的主程序，约 900 行，承担：

- 初始化：camera.ini、lcd.ini、log、lcd、camera、touch、http；
- 主循环 `poll`：
  - 摄像头 fd；
  - 触摸 fd；
  - HTTP listen fd；
  - 按键 fd（`lc-button`）；
- 触摸：左半边 `g_save=1`，右半边 `g_res=1`；
- 按键：`KEY_F1` → 拍照，`KEY_F2` → 切分辨率；
- 每帧：DQBUF → YUYV→RGB → QBUF → 按行写 → OSD → flip → 统计；
- `g_save`：`snapshot_mjpeg()`（STOP→MJPG→取一帧→写 jpg→切回 YUYV）；
- `g_res`：`res_switch()` 轮换 800×600 / 640×480 / 352×288。

注意：`g_save` / `g_res` 块在“取帧并上屏”之后。按键唤醒 poll 但没有摄像头帧时，
代码会 `continue`，`g_save/g_res` 会保留到下一帧再处理。

---

## 5. v1.5 网页

### 5.1 接口

```text
http://192.168.10.50:8888/                首页（HTML + <img src="/stream">）
http://192.168.10.50:8888/stream          multipart/x-mixed-replace MJPEG
http://192.168.10.50:8888/snapshot.jpg    单张 JPEG
```

### 5.2 行为

- 推流时摄像头切 MJPG，**本地 LCD 预览暂停**；浏览器断开后恢复 YUYV；
- 单客户端、阻塞式；
- 浏览器预连接（空连接）300ms 没请求就关，避免堵住主循环；
- 网页“拍一张”只把图发给浏览器，**不写 `images/`**；存卡用触摸/`SIGUSR1`；
- iOS Safari 不支持 MJPEG 流，只能看单张。

### 5.3 实测

- 延迟：800×600 约 311ms，640×480 约 276ms（截图法）；
- 1 小时推流：26856 帧、dropped 8、RSS/fd 不增长；
- 10 次开关标签页稳定。

---

## 6. W8–W9 驱动

详细内容见 `docs/driver.md`，这里只放“新会话必须知道”的部分。

### 6.1 已完成

- `hello_misc`：misc 字符设备，read/write/ioctl/poll/等待队列；
- `button_plat`：platform 驱动 + input 子系统，两个 GPIO、两个键；
- `ap3216c`：i2c_driver + IIO，光感以 `in_illuminance_input` 形式暴露；
- 设备树：`driver/button_plat/lc_button.patch`，以及追加到 `.dts` 末尾的 `&i2c1 { ap3216c@1e; }`；
- 应用已接入：按键（按名字找 `lc-button`）、上下滑调亮度、按环境光自动调亮度。

### 6.2 当前行为

- KEY1 拍照、KEY2 切分辨率都验证通过；
- `dmesg` 应该有两行：

```text
button: 0 irq=... key=59
button: 1 irq=... key=60
```

- 如果只有一行，说明跑的是旧单键 `.ko`：`rmmod` 后重新 `insmod` 新编的；
- 应用日志里会有 `button: /dev/input/eventX`。

光感和亮度这边：

- 驱动加载时 `dmesg` 有一行 `AP3216C light sensor registered`，
  应用日志是 `lux: /sys/bus/iio/devices/iio:deviceX/in_illuminance_input`；
- 驱动没加载时退回直读，应用日志是 `lux: /dev/i2c-0 addr 0x1e (i2c-dev)`，
  OSD 上的 lux 数值一样；
- 自动亮度每 200ms 采一次，连续 3 次一致才改，一次走一档；
  上电有 1 秒宽限，手动滑动后 30 秒内不再自动调；
- 实测：全黑 0 → 25%，室内 80–90 → 75%，手电约 1000 → 100%。

### 6.3 已知问题

- `poll revents mismatch` 的 WARN 已经注释掉，日志里不该再出现这条；
- 驱动假设 `button-gpios` 里正好两个 GPIO；只写一个会 probe 失败；
- `gpio-keys` 必须 `disabled`，否则 GPIO 被占；
- 板子上没有 `evtest`；可以用 `dmesg` 里的 `evbug`，或 `hexdump -C /dev/input/eventX`；
- 驱动加载后 `i2cget -y 0 0x1e ...` 会报 `Device or resource busy`，属正常；
- IIO 设备号不固定（`iio:device0/1/2`），应用按 `name` 找，不写死编号；
- 改完任何 `.ko` 都要 `rmmod` 再 `insmod`，否则跑的是旧模块；
- DTS patch 在 Windows 上生成容易变成 UTF-16，`patch` 会报
  “Only garbage was found”；要转 ASCII/UTF-8，或在 Ubuntu 上用 `diff` 生成。

### 6.4 还没做

- OSD 设置菜单：亮度、熄屏、governor；
- 锁定触摸屏：点图标后禁掉拍照/切分辨率，滑动和按键保留；
- 相册 / Web 管理；
- v2.0：线程化（本地预览 + 推流同时）、拆 `app.c`、单元测试、
  Buildroot 精简 + 开机自启 + 看门狗。

---

## 7. 构建 / 部署 / 运行

### 7.1 用户态

```bash
cd ~/Desktop/LinuxCamera

cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-arm.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-arm -j"$(nproc)"
```

部署：

```bash
adb push build-arm/test_camera_preview /root/LinuxCamera/
adb push config/*.ini /root/LinuxCamera/config/
adb push scripts/soak_test.sh scripts/mon.sh /root/LinuxCamera/
```

运行：

```bash
cd /root/LinuxCamera
./test_camera_preview
```

### 7.2 内核模块

```bash
cd driver/hello_misc && make
cd ../button_plat && make
cd ../ap3216c && make

adb push hello_misc.ko button_plat.ko ap3216c.ko /root/
adb shell rmmod button_plat 2>/dev/null
adb shell insmod /root/button_plat.ko
adb shell rmmod ap3216c 2>/dev/null
adb shell insmod /root/ap3216c.ko
adb shell dmesg | tail
```

模块必须用板子对应的内核树编译；`insmod` 已加载模块会失败，先 `rmmod`。
`ap3216c` 要求内核开了 `CONFIG_IIO`；`=m` 时板上还要有 `industrialio.ko`。

### 7.3 设备树

```bash
cd ~/100ask_imx6ull-sdk/Linux-4.9.88
patch -p1 --dry-run < ~/LinuxCamera/driver/button_plat/lc_button.patch
patch -p1 < ~/LinuxCamera/driver/button_plat/lc_button.patch

make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- \
     arch/arm/boot/dts/100ask_imx6ull-14x14.dtb
```

备份板子 `/boot/100ask_imx6ull-14x14.dtb`，推新的、`sync`、重启。

光感的节点是**追加**到 `.dts` 末尾的，不叠 patch：

```dts
&i2c1 {
	status = "okay";

	ap3216c@1e {
		compatible = "linuxcamera,ap3216c";
		reg = <0x1e>;
	};
};
```

追加完一样重编 dtb、替换、`sync` 两次、重启，然后确认：

```bash
ls /proc/device-tree/i2c@21a0000/ | grep ap3216c
```

`driver/ap3216c/ap3216c.patch` 是从内核树 `git diff` 出的整份 `.dts` 差异，
里面也包含 lc_button 的改动，只用于记录，别和 `lc_button.patch` 叠着打。

### 7.4 网络/网页

板子 IP `192.168.10.50`，浏览器开 `http://192.168.10.50:8888/`。

### 7.5 长跑

```bash
nohup ./test_camera_preview >/dev/null 2>&1 &
nohup ./soak_test.sh 8 >/dev/null 2>&1 &
tail -f soak/soak_*.log
```

`mon.sh` 每秒记录 CPU/RSS/fd，推流时用。

---

## 8. 测试数据

| 测试 | 结果 |
|---|---|
| v0.5 10 分钟 | 604s / 9036 帧 / 15.0fps / 0 dropped |
| v1.0 8 小时 | 610390 帧 / 47 张照片 / 15 次切换 / 0 错误 / RSS/fd 不涨 |
| v1.5 1 小时推流 | 26856 帧 / dropped 8 / RSS/fd 不涨 |
| 延迟 | 800×600 约 311ms；640×480 约 276ms |
| 开关标签页 10 次 | 稳定，dropped 0 |
| 按键 | KEY1 拍照、KEY2 切分辨率，均验证 |

详细数据在 `docs/performance.md`。

---

## 9. 已知问题 / 坑

### 应用

1. `test_camera_preview.c` 约 900 行，采集/显示/触摸/按键/HTTP/OSD 都在里面，v2.0 要拆；
2. `lcd_write_line_rgb888` 只支持 32bpp；
3. `yuyv_to_rgb` 没有 stride，只支持 `bytesperline == width*2`；
4. `yuyv.c` 全局 `full`；
5. `lcd.c` 全局状态；`lcd_init` 重置日志、`lcd_deinit` 关闭日志；
6. `app_config_t` 太大，没有 touch/OSD/http 配置；
7. 首帧 1–1.6s；切 800×600 固定约 1.7s；
8. HTTP 单客户端、阻塞；推流期间触摸/按键不响应，退出后可能补触发一次；
9. 网页拍照不落盘；
10. 推流时 LCD 冻结，不能双端同时；
11. iOS Safari 不支持 MJPEG 流；
12. 跑满 10 分钟 LCD 会被 console blank，`FBIOPAN_DISPLAY` 报错并降级单缓冲。

### 驱动

1. 驱动假设两个 GPIO；只写一个会 probe 失败；
2. `gpio-keys` 必须 disabled；
3. 改完模块不 `rmmod` 会加载旧 `.ko`，表现“代码没生效”；
4. DTS patch 编码/路径问题；
5. 光感驱动占了 `0x1e`，`i2cget` 会 `EBUSY`，属正常；
6. 没有 `evtest`，用 evbug/hexdump；
7. IIO 设备号不固定，应用按 `name` 找。

### 构建

- `build-arm` 出现异常时先删掉重编；
- 内核模块必须和板子内核版本/配置一致；
- Windows 上生成 patch 注意 UTF-16。

---

## 10. 下一步

### 马上

1. 锁死当前状态：`git status` 干净，`driver/ap3216c/` 和 `lux`、`autolight` 都已提交；
2. 板子上把三轮演示跑一遍：预览 + KEY1/KEY2 + 上滑/下滑 + 遮光/手电。

### 产品侧（M1 收尾）

1. 锁定触摸屏：右黑边放个图标，点了就禁掉拍照/切分辨率，滑动和按键保留；
2. OSD 设置菜单：亮度、熄屏、governor；
3. 再往后是相册/Web 管理。

### v2.0

- 线程化（本地预览 + 推流同时，先测 libjpeg-turbo 编解码开销）；
- 拆 `app.c` / `display.c`，`main.c` 只做编排；
- 单元测试（`yuyv`、`config`、`autolight` 都能脱硬件测）；
- Buildroot 精简 + 开机自启 + 看门狗；
- 再往后的多客户端/鉴权/HTTPS。

---

## 11. 不要做的事

- 没让改就不要改；改之前先说明原因和范围；
- 不要动 `docs/debug-log.md`；
- 不要为了“架构漂亮”大改 `config.c` 或重写核心数据通路；
- 不要假设摄像头能同时输出 YUYV 和 MJPG（只有一路）；
- 不要在推流时忘了 `SIGPIPE`、部分写、客户端断开；
- 不要用 Windows MSVC 编 V4L2；
- 不要在未确认实际分辨率时假设 1024×600；
- 不要把 `camera.log`、`soak/`、`images/`、`*.dts`、`.ko` 提交进仓库；
- 改完内核模块一定要 `rmmod` 再 `insmod`；
- 不要把亮度阈值、迟滞这些策略写进驱动，驱动只搬数据；
- 不要把 IIO 设备号（`iio:device0`）写死，按 `name` 找。

---

## 12. 新会话检查清单

1. 读本文；
2. `git status` / `git log --oneline -5` / `git tag -l`；
3. 读 `docs/driver.md`、`docs/architecture.md`、`docs/board.md`、`docs/roadmap.md`；
4. 确认 `camera.ini` 的 `format=YUYV`；
5. 确认板子 IP、摄像头 800×600、LCD 1024×600；
6. 构建用户态，上板跑 30 秒，确认 15fps、`DQbuf == Qbuf == Frames`、无 ERROR；
7. 确认 `lsmod | grep button`、`dmesg | grep button` 有两行；
8. 按 KEY1/KEY2，确认拍照/切分辨率；
9. `insmod ap3216c.ko`，确认 `in_illuminance_input` 有值、遮住会降；
10. 再开始产品侧（锁定屏/设置菜单）或 v2.0。

---

## 13. 文档索引

| 文档 | 内容 |
|---|---|
| `README.md` | 对外入口、构建/运行、功能表 |
| `docs/handoff.md` | 本文 |
| `docs/driver.md` | W8–W9 驱动：hello_misc、button_plat、ap3216c、DTS、IIO |
| `docs/architecture.md` | 模块、数据流、技术债 |
| `docs/board.md` | 板子、网络、部署、gdb、长跑 |
| `docs/roadmap.md` | 版本计划、memo 产品想法、backlog |
| `docs/performance.md` | 8 小时、1 小时推流、延迟数据 |
| `docs/release-v1.0.md` | v1.0 发布说明 |
| `docs/release-v1.5.md` | v1.5 发布说明 |
| `docs/debug-log.md` | 用户自己的踩坑记录（不要改） |

---

## 14. 最后一句

> **v1.5 已经能演示；按键、背光、光感都用自写驱动接进来了。
> 接下来是产品侧（锁定屏、设置菜单），最后才是 v2.0 的重构。**
