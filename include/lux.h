#ifndef _LUX_H
#define _LUX_H

/*
 * 修改：光感读取模块（AP3216C，挂在 /dev/i2c-0 的 0x1e）。
 *
 * 这一版只做用户态直读：
 *   open 总线 -> I2C_SLAVE -> 0x00 写 0x03 使能 -> 读 0x0C/0x0D
 * 等 C 阶段的 ap3216c 驱动上了，地址会被内核占住（EBUSY），
 * 那时再在 lux_open() 里加一条 sysfs 分支，接口不变。
 */

#define LUX_DEFAULT_DEV     "/dev/i2c-0"
#define LUX_DEFAULT_ADDR    0x1e

typedef struct {
	int fd;
	int addr;
} lux_t;

int  lux_open(lux_t *lx, const char *dev, int addr);
int  lux_read(lux_t *lx, int *lux);
void lux_close(lux_t *lx);

#endif
