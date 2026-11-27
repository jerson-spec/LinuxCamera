# driver

W8–W9 的内核模块练习，和用户态的 `src/`、`tests/` 分开，不参与 CMake。

## 目录

- `hello_misc/`：misc 字符设备，支持 read/write/ioctl/poll。
  用来练 `file_operations`、`copy_to/from_user`、等待队列和 `poll_wait`。
- `button_plat/`：platform + input 按键驱动，设备树匹配 `linuxcamera,button`，
  中断里用 `input_report_key()` 上报，用户态从 `/dev/input/eventX` 读。
  设备树补丁在 `button_plat/lc_button.patch`。
- `ap3216c/`：I2C 光感 + IIO，设备树匹配 `linuxcamera,ap3216c`，
  用户态读 `/sys/bus/iio/devices/.../in_illuminance_input`。
  设备树是追加到 `.dts` 末尾的，差异存在 `ap3216c/ap3216c.patch`。

更完整的驱动记录（DTS、按键、光感、IIO、踩坑）见 `docs/driver.md`。

新驱动继续按目录加，每个目录自带 Makefile、测试程序和 README。

## 编译

先确认 SDK 内核源码路径（默认 `~/100ask_imx6ull-sdk/Linux-4.9.88`，
并且这棵树已经编译过），然后：

```bash
cd driver/hello_misc
make

cd ../button_plat
make

cd ../ap3216c
make
```

会生成 `hello_misc.ko` / `test_hello`、`button_plat.ko`、`ap3216c.ko`。
如果工具链不在 PATH 里：

```bash
make CROSS_COMPILE=/path/to/bin/arm-buildroot-linux-gnueabihf-
```

## 上板

```bash
adb push hello_misc.ko test_hello button_plat.ko /root/
adb shell
insmod /root/hello_misc.ko
chmod 666 /dev/hello_misc
dmesg | tail

rmmod button_plat 2>/dev/null
insmod /root/button_plat.ko
dmesg | tail

rmmod ap3216c 2>/dev/null
insmod /root/ap3216c.ko
dmesg | tail
cat /sys/bus/iio/devices/iio:device*/in_illuminance_input
```

`ap3216c` 需要内核开 `CONFIG_IIO`；`=m` 时板上还要有 `industrialio.ko`。

具体接口和测试步骤见各模块目录下的 README。

## 注意

- 模块必须用**板子对应的内核树**编译，否则 `insmod` 会报 `Invalid module format`；
- 内核模块和用户态 CMake 是两套构建，不要混在一起；
- `.ko`、`.o`、`*.mod.c`、`.tmp_versions/` 这些编译产物已经在 `.gitignore` 里。
