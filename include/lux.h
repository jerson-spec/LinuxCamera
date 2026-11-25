#ifndef _LUX_H
#define _LUX_H

/*
 * 修改：光感读取模块（AP3216C，挂在 /dev/i2c-0 的 0x1e）。
 *
 * 两条后端，open 时自动选：
 *   1) 内核驱动 ap3216c 已加载 -> 读
 *      /sys/bus/iio/devices/iio:deviceN/in_illuminance_input
 *   2) 没有驱动 -> 直接读写 /dev/i2c-0
 *
 * 不能同时走：i2c-dev 在驱动占用地址后返回 EBUSY。
 * 选哪条是 open 时定的，切换后端需要重启程序。
 */

#define LUX_DEFAULT_DEV     "/dev/i2c-0"
#define LUX_DEFAULT_ADDR    0x1e

typedef struct {
	int  fd;            /* i2c 直读用的 fd，-1 表示没打开 */
	int  addr;
	int  iio;           /* 1 = 走 sysfs，0 = 走 i2c-dev */
	char path[300];     /* iio 属性文件路径 */
} lux_t;

int  lux_open(lux_t *lx, const char *dev, int addr);
int  lux_read(lux_t *lx, int *lux);
void lux_close(lux_t *lx);

#endif
