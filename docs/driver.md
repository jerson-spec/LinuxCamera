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

## 5. 还没做：I2C 光感

memo 里提到“板子上的光感传感器，是 I2C 的”，但当前 DTS 的 `&i2c1` 是空的，
没有 AP3216C 之类的子节点。

先确认硬件：

```bash
ls /dev/i2c-*
i2cdetect -y 1
```

- 如果看到 `0x1e`，大概率是 AP3216C；
- 先用 `/dev/i2c-1` + `I2C_SLAVE` 在用户态读寄存器，确认型号和寄存器；
- 再写 `i2c_driver`（`compatible`、`probe`、`i2c_smbus_read_byte_data`），
  暴露一个 sysfs 属性或 misc 设备给应用读 lux；
- 没有光感的话，这条功能先跳过，不要为了凑功能硬做。

背光这边 DTS 已经有 `pwm-backlight`（`pwms = <&pwm1 0 1000>`，亮度 0–10），
板子上应该有 `/sys/class/backlight/backlight/brightness`。光感/上滑调亮度的前提是
先确认这个节点存在。

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
```

模块必须用**板子对应的内核树**编译，否则 `insmod` 会报
`Invalid module format`（vermagic 不匹配）。`Makefile` 里
`KDIR=$(HOME)/100ask_imx6ull-sdk/Linux-4.9.88`，换路径就传 `KDIR=...`。

`insmod` 一个已加载的模块会失败（`File exists`），改完代码一定要先 `rmmod`。
