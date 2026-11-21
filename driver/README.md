# driver

W8–W9 的内核模块练习，和用户态的 `src/`、`tests/` 分开，不参与 CMake。

## 目录

- `hello_misc/`：misc 字符设备，支持 read/write/ioctl/poll。
  用来练 `file_operations`、`copy_to/from_user`、等待队列和 `poll_wait`。

以后每学一个驱动就加一个目录，比如 `button_input/`、`i2c_sensor/`，
每个目录自带 Makefile、测试程序和 README。

## 编译

先确认 SDK 内核源码路径（默认 `~/100ask_imx6ull-sdk/Linux-4.9.88`，
并且这棵树已经编译过），然后：

```bash
cd driver/hello_misc
make
```

会生成 `hello_misc.ko` 和交叉编译的 `test_hello`。
如果工具链不在 PATH 里：

```bash
make CROSS_COMPILE=/path/to/bin/arm-buildroot-linux-gnueabihf-
```

## 上板

```bash
adb push hello_misc.ko test_hello /root/
adb shell
insmod /root/hello_misc.ko
chmod 666 /dev/hello_misc
dmesg | tail
/root/test_hello
rmmod hello_misc
```

具体接口和测试步骤见各模块目录下的 README。

## 注意

- 模块必须用**板子对应的内核树**编译，否则 `insmod` 会报 `Invalid module format`；
- 内核模块和用户态 CMake 是两套构建，不要混在一起；
- `.ko`、`.o`、`*.mod.c`、`.tmp_versions/` 这些编译产物已经在 `.gitignore` 里。
