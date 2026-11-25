# ap3216c

AP3216C 环境光驱动的 IIO 版本。

## 硬件位置

| 项目 | 值 |
|---|---|
| 总线 | `21a0000.i2c`，设备树 `&i2c1`，用户态 `/dev/i2c-0` |
| 地址 | `0x1e` |
| 上报 | `/sys/bus/iio/devices/iio:deviceN/in_illuminance_input`（lux） |

编号差一位是常态：设备树叫 `i2c1`，用户态是 `i2c-0`。以 `i2cdetect -l`
打出来的 `21a0000.i2c` 为准。

## 前置

1. 内核开了 IIO：

   ```bash
   grep -E "^CONFIG_IIO(=|_| )" ~/100ask_imx6ull-sdk/Linux-4.9.88/.config
   ```

   `=y` 或 `=m`。如果是 `=m`，板上还要有 `industrialio.ko`，没有就一起推过去。

2. 内核树已经编译过（有 `Module.symvers`、`scripts/`），否则 `make` 直接失败。

## 设备树

追加，不需要打 patch：

```bash
cd ~/100ask_imx6ull-sdk/Linux-4.9.88
DTS=arch/arm/boot/dts/100ask_imx6ull-14x14.dts

grep -q "linuxcamera,ap3216c" $DTS && echo "already" || \
cat >> $DTS <<'EOF'

&i2c1 {
	status = "okay";

	ap3216c@1e {
		compatible = "linuxcamera,ap3216c";
		reg = <0x1e>;
	};
};
EOF

make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- \
     arch/arm/boot/dts/100ask_imx6ull-14x14.dtb
```

备份板子上的 dtb，推新的，`sync` 两次再重启：

```bash
adb shell cp /boot/100ask_imx6ull-14x14.dtb /boot/dtb.bak
adb push arch/arm/boot/dts/100ask_imx6ull-14x14.dtb /boot/
adb shell sync && adb shell sync
adb shell reboot
```

重启后确认节点进去了：

```bash
ls /proc/device-tree/i2c@21a0000/ | grep ap3216c
```

这一步不能省：后面 `insmod` 报 `-ENODEV` 时，全靠它区分“dtb 没换”还是“驱动写错”。

## 编译 / 加载

```bash
cd ~/LinuxCamera/driver/ap3216c
make
adb push ap3216c.ko /root/
adb shell
insmod /root/ap3216c.ko
dmesg | tail
```

期望：

```text
ap3216c 0-001e: AP3216C light sensor registered
```

## 验证

```bash
ls /sys/bus/iio/devices/
cat /sys/bus/iio/devices/iio:device*/name            # ap3216c
cat /sys/bus/iio/devices/iio:device*/in_illuminance_input
```

手遮住应该掉下去，手电照应该升上去，数值和同时跑的 `./test_lux` 对得上
（差几 lux 正常，采样时刻不同）。

`i2cget -y 0 0x1e 0x0c` 此时会报 `Device or resource busy` —— 这是驱动
占住地址的证据，不是故障。`rmmod ap3216c` 就恢复。

## 边界

驱动只负责把 lux 读出来，不做任何亮度决策。阈值、迟滞、渐变在
`src/autolight.c`；应用暂时还在走 `/dev/i2c-0` 直读，等驱动验证通过后
再给 `lux_open()` 加 sysfs 分支。
