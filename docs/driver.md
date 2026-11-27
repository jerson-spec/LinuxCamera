# 驱动记录（W8–W9）

这份文档只记内核模块这一摊：为什么做、做到哪、怎么编译、怎么上板、踩过什么坑。
应用层的东西看 `docs/architecture.md`，板子/网络看 `docs/board.md`。

---

## 1. 为什么做驱动

v1.5 之前，项目一直是“用户态消费内核接口”：

- `open("/dev/video1")` + `ioctl(VIDIOC_*)`；
- `mmap` framebuffer + `FBIOPAN_DISPLAY`；
- `read("/dev/input/event1")` 读触摸。

W8–W9 是把这条线反过来走一遍：自己写字符设备、platform 驱动、input 设备、
I2C 设备，知道 `/dev/video1`、`/dev/input/eventX`、`/sys/class/...` 这些节点是怎么来的。

同时它也直接服务项目功能：

- 板载按键 → 拍照 / 切分辨率；
- 光感（I2C）→ 自动亮度；
- 后面 OSD 设置、相册也会用到这些接口。

---

## 2. 目录和文件

```text
driver/
├── README.md
├── hello_misc/
│   ├── hello_misc.c
│   ├── Makefile
│   ├── README.md
│   └── test_hello.c
└── button_plat/
    ├── button_plat.c
    ├── Makefile
    └── lc_button.patch      # 设备树改动，应用到内核树

├── ap3216c/
│   ├── ap3216c.c
│   ├── Makefile
│   ├── ap3216c.patch      # 内核树 .dts 的差异，重建环境用
│   └── README.md
```

`driver/` 不参与顶层 CMake，和用户态程序是两套构建。

---

## 3. `hello_misc`：字符设备 + poll

目的：练 `file_operations`、`copy_to/from_user`、等待队列、`poll_wait`。

设备节点：`/dev/hello_misc`（misc 设备，动态 minor）。

接口：

| 操作 | 行为 |
|---|---|
| `write` | 最多 255 字节，存到内核缓冲区，重置读位置，唤醒等待者 |
| `read` | 读回缓冲区；没数据时阻塞，`O_NONBLOCK` 返回 `-EAGAIN` |
| `ioctl(HELLO_IOC_RESET)` | 清空缓冲区 |
| `ioctl(HELLO_IOC_GETLEN)` | 取当前长度 |
| `poll` | 有数据时返回 `POLLIN` |

命令号：

```c
#define HELLO_IOC_MAGIC   'h'
#define HELLO_IOC_RESET   _IO(HELLO_IOC_MAGIC, 0)
#define HELLO_IOC_GETLEN  _IOR(HELLO_IOC_MAGIC, 1, int)
```

`'h'` 是随手选的魔数，正式驱动要去 `Documentation/ioctl/ioctl-number.txt` 登记。

已修的坑：`read` 的等待条件原来只判断 `hello_len > 0`，数据读完后会直接返回 0，
而不是继续等；`poll` 也会一直报告可读。现在改成 `hello_len > *ppos` /
`hello_len > file->f_pos`。

编译 / 上板：

```bash
cd driver/hello_misc
make
adb push hello_misc.ko test_hello /root/
adb shell insmod /root/hello_misc.ko
adb shell chmod 666 /dev/hello_misc
adb shell /root/test_hello /dev/hello_misc
```

`test_hello` 现在是 poll 测试：先 `RESET`，再等 5 秒；另一个终端
`echo hello > /dev/hello_misc` 唤醒它。

---

## 4. `button_plat`：platform + input 按键驱动

### 4.1 硬件

原理图上的两个按键：

| 按键 | GPIO | 有效电平 | 外围 |
|---|---|---|---|
| KEY1 | GPIO5_IO01（SNVS_TAMPER1） | 低有效 | 10K 上拉、470R、TVS、100nF |
| KEY2 | GPIO4_IO14（NAND_CE1_B） | 低有效 | 同上 |

按下 = 拉低，松开 = 上拉回 3V3。

### 4.2 设备树

`driver/button_plat/lc_button.patch` 是相对原始
`arch/arm/boot/dts/100ask_imx6ull-14x14.dts` 的补丁，包含四块：

1. 根节点下新增 `lc_button`：

```dts
lc_button {
    compatible = "linuxcamera,button";
    button-gpios = <&gpio5 1 GPIO_ACTIVE_LOW
                    &gpio4 14 GPIO_ACTIVE_LOW>;
    pinctrl-names = "default";
    pinctrl-0 = <&key1_100ask &key2_100ask>;
};
```

2. `gpio-keys` 加 `status = "disabled"`，避免和我们的驱动抢 GPIO；
3. `&iomuxc` 里新增 `key2_100ask`：`MX6UL_PAD_NAND_CE1_B__GPIO4_IO14 0x000010B0`；
4. `&iomuxc_snvs` 里新增 `key1_100ask`：`MX6ULL_PAD_SNVS_TAMPER1__GPIO5_IO01 0x000110A0`。

DTS 里 `&gpmi` 是 `disabled`，所以 NAND 不会占 GPIO4_IO14。

应用补丁：

```bash
cd ~/100ask_imx6ull-sdk/Linux-4.9.88
patch -p1 --dry-run < ~/LinuxCamera/driver/button_plat/lc_button.patch
patch -p1 < ~/LinuxCamera/driver/button_plat/lc_button.patch

make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- \
     arch/arm/boot/dts/100ask_imx6ull-14x14.dtb
```

然后备份板子 `/boot/100ask_imx6ull-14x14.dtb`，推新的、`sync`、重启。

### 4.3 驱动

`button_plat.c` 做四件事：

1. 申请一个 input 设备，名字 `"lc-button"`；
2. 按键表：

```c
static const unsigned int button_keycodes[] = {
    KEY_F1,     /* KEY1：拍照 */
    KEY_F2,     /* KEY2：切分辨率 */
};
```

3. 按表循环，`devm_gpiod_get_index(..., "button", i, GPIOD_IN)` 拿第 i 个 GPIO，
   `gpiod_to_irq()` 拿中断，`devm_request_irq()` 双边沿注册；
4. 中断里读逻辑电平、去抖、`input_report_key()` + `input_sync()`。

几个细节：

- `gpiod_get_value()` 返回的是**逻辑值**：`GPIO_ACTIVE_LOW` 时按下就是 1，
  不要再取反；
- 去抖只在“按下”沿做 20ms，松开沿不丢，避免只报按下不报松开；
- `last_state` 防止重复上报；
- `devm_*` 负责卸载时释放，不需要手写 `remove()`。

### 4.4 用户态

应用 `test_camera_preview.c` 里：

- 用 `input_find_event("lc-button")` 扫描 `/sys/class/input/event*/device/name`，
  按名字找 event 节点，不写死 event 号；
- 把按键 fd 加进 `poll`；
- `handle_buttons()` 只处理 `EV_KEY` 且 `value == 1`：
  - `KEY_F1` → `g_save = 1`；
  - `KEY_F2` → `g_res = 1`。

拍照/切分辨率还是走原来的 `g_save` / `g_res` 逻辑。

### 4.5 当前状态

- KEY1 拍照、KEY2 切分辨率都验证通过；
- 加载模块后 `dmesg` 应该有两行：

```text
button: 0 irq=... key=59
button: 1 irq=... key=60
```

- 如果只有一行，说明跑的还是旧的单键 `.ko`，先 `rmmod` 再 `insmod` 新编的；
- 应用日志里会有 `button: /dev/input/eventX`。

### 4.6 已知问题

- `test_camera_preview.c` 里还有一条过时的
  `LOG_WARN("poll revents mismatch: %hd", ...)`；
  按键/触摸/HTTP 唤醒 poll 而摄像头没帧时就会打印，属于噪音，不过已经加注释处理完毕；
- 驱动默认 `button-gpios` 里正好两个 GPIO；只写一个的话，第二个
  `devm_gpiod_get_index(..., 1, ...)` 会失败，probe 失败；
- `gpio-keys` 必须 `disabled`，否则 GPIO 被占，probe 会 `-EBUSY`；
- 板子上没有 `evtest`，可以用 `dmesg` 里的 `evbug` 事件，或者
  `hexdump -C /dev/input/eventX` 粗看；
- `lc_button.patch` 在 Windows 上生成时容易变成 UTF-16，`patch` 会报
  “Only garbage was found”；要转成 ASCII/UTF-8，或在 Ubuntu 上用 `diff` 重新生成。

---

## 5. `ap3216c`：I2C 光感 + IIO

### 5.1 先摸清硬件

之前一直以为光感在 `i2c1`，实际是这么对上的：

```bash
ls /dev/i2c-*
i2cdetect -l          # 21a0000.i2c → i2c-0；21a4000.i2c → i2c-1
i2cdetect -y 0        # 0x1e 上有个设备
i2cdetect -y 1        # 只有触摸、EEPROM 之类，没有光感
```

| 项目 | 值 |
|---|---|
| 总线 | `21a0000.i2c`，设备树 `&i2c1`，用户态 `/dev/i2c-0` |
| 地址 | `0x1e` |
| 型号 | AP3216C（写 `0x00=0x03` 能回读 `0x03`） |
| 用到的通道 | 只读 ALS（环境光），PS/IR 没用 |

**设备树叫 `i2c1`，用户态是 `i2c-0`，编号差一位。** 之前文档里写的
`i2cdetect -y 1` 就是这么错的，以后统一以 `i2cdetect -l` 打出来的
`21a0000.i2c` 为准。

### 5.2 用户态先把寄存器和换算走通

写驱动之前先用 i2c-tools 确认，别拿猜测的寄存器表去写内核代码：

```bash
i2cset -y 0 0x1e 0x00 0x03      # 使能 ALS+PS+IR
i2cget -y 0 0x1e 0x00           # 期望回读 0x03
i2cget -y 0 0x1e 0x0c           # ALS 低字节
i2cget -y 0 0x1e 0x0d           # ALS 高字节，bit7 是有效标志
```

```text
ALS = ((高字节 & 0x7F) << 8) | 低字节
lux = ALS * 0.35
```

实测四组数，后来直接成了自动亮度的阈值依据：

| 场景 | lux |
|---|---|
| 全黑 / 手遮住 | 0 |
| 刚上电那几拍 | 0–3（不可信，见 5.6） |
| 室内常态 | 80–90 |
| 手电贴着照 | ~1000 |

这一版的用户态代码是 `src/lux.c` + `tests/manual/test_lux.c`。

### 5.3 设备树

追加到 `.dts` 末尾，不叠 patch——`lc_button.patch` 已经把文件改过一轮，
再按行号插一个 patch 很容易对不上：

```dts
&i2c1 {
	status = "okay";

	ap3216c@1e {
		compatible = "linuxcamera,ap3216c";
		reg = <0x1e>;
	};
};
```

重编 dtb、备份、替换、`sync` 两次、重启，然后确认节点：

```bash
ls /proc/device-tree/i2c@21a0000/ | grep ap3216c
```

`insmod` 报 `-ENODEV` 时先查这一行：有节点说明问题在驱动，没节点说明
dtb 根本没换。

`driver/ap3216c/ap3216c.patch` 是从内核树 `git diff` 出来的整份 `.dts`
差异，**包含之前 lc_button 的改动**。它只用来说明“当时树里是什么样”，
重建环境时和 `lc_button.patch` 不要叠着打。

### 5.4 驱动做了什么

`driver/ap3216c/ap3216c.c`，`i2c_driver` + IIO，约 150 行。

| 部分 | 内容 |
|---|---|
| 匹配 | `compatible = "linuxcamera,ap3216c"` |
| probe | 检查 SMBUS 能力 → `devm_iio_device_alloc` → 写 `0x00=0x03` → `msleep(20)` → 注册 |
| 通道 | 一个：`IIO_LIGHT` + `IIO_CHAN_INFO_PROCESSED` |
| 读数 | `read_raw` 里读 `0x0C/0x0D`，乘 0.35，返回 `IIO_VAL_INT` |
| 释放 | 全走 `devm_*`，所以没写 `remove()` |

用户态看到的是：

```bash
cat /sys/bus/iio/devices/iio:deviceN/in_illuminance_input
```

为什么用 `processed` 而不是 `raw + scale`：`als * 0.35` 是芯片特性，
不是产品逻辑。换算放驱动，换传感器时用户态不用改；阈值、迟滞、档位仍然
留在应用里——**驱动只搬数据，不做决策**。

### 5.5 两个坑

- 驱动加载后 `i2cget -y 0 0x1e ...` 会报 `Device or resource busy`。
  这是地址被占住的证据，不是故障；`rmmod ap3216c` 就恢复；
- **IIO 设备号不固定**（`iio:device0/1/2`，看加载顺序）。用户态必须按
  `device/name` 找，不能写死编号，`src/lux.c` 里就是扫名字。

### 5.6 应用侧两条后端

`lux_open()` 先找 IIO 节点，找不到再退回直读：

```text
驱动已加载  → /sys/bus/iio/devices/.../in_illuminance_input
驱动没加载  → open /dev/i2c-0 + I2C_SLAVE 0x1e
```

两条不能同时走（地址只有一个主人），走哪条在 `open` 时定，切换要重启程序。
这样调试时 `rmmod` 就能回直读，演示时装驱动走标准接口，主程序一行不用改。

自动亮度为什么有 1 秒启动宽限：als 刚使能时读数是 0–3，不可信。`probe` 里
`msleep(20)` 只够芯片应答，不够积分稳定，所以策略层再等 1 秒。

### 5.7 编译 / 上板

```bash
cd driver/ap3216c && make
adb push ap3216c.ko /root/
adb shell rmmod ap3216c 2>/dev/null
adb shell insmod /root/ap3216c.ko
adb shell dmesg | tail
adb shell 'cat /sys/bus/iio/devices/iio:device*/in_illuminance_input'
```

期望 `dmesg` 有一行 `AP3216C light sensor registered`。

内核没开 `CONFIG_IIO` 会编不过或加载失败；`=m` 时板上还得有
`industrialio.ko`。改完 `.ko` 记得先 `rmmod` 再 `insmod`，这条对所有模块都成立。

---

## 6. 编译 / 加载 / 调试速查

```bash
# hello_misc
cd driver/hello_misc && make
adb push hello_misc.ko test_hello /root/
adb shell insmod /root/hello_misc.ko

# button_plat
cd driver/button_plat && make
adb push button_plat.ko /root/
adb shell rmmod button_plat 2>/dev/null
adb shell insmod /root/button_plat.ko
adb shell dmesg | tail

# 按键事件（没有 evtest）
adb shell dmesg -c >/dev/null
# 按 KEY1 / KEY2
adb shell dmesg | tail -20

# ap3216c（光感）
cd driver/ap3216c && make
adb push ap3216c.ko /root/
adb shell rmmod ap3216c 2>/dev/null
adb shell insmod /root/ap3216c.ko
adb shell dmesg | tail
adb shell 'cat /sys/bus/iio/devices/iio:device*/in_illuminance_input'

# 退回用户态直读
adb shell rmmod ap3216c
```

模块必须用**板子对应的内核树**编译，否则 `insmod` 会报
`Invalid module format`（vermagic 不匹配）。`Makefile` 里
`KDIR=$(HOME)/100ask_imx6ull-sdk/Linux-4.9.88`，换路径就传 `KDIR=...`。

`insmod` 一个已加载的模块会失败（`File exists`），改完代码一定要先 `rmmod`。
