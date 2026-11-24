/*
 * 修改：新增光感读取模块（用户态直读）。
 *
 * 设备是 AP3216C，挂在 21a0000.i2c（用户态 /dev/i2c-0）地址 0x1e。
 * 寄存器口径和 A 阶段 i2cset/i2cget 验证过的完全一致：
 *   0x00 写 0x03 使能 ALS+PS+IR，0x0C/0x0D 读 ALS 低/高字节。
 * 只读 ALS（环境光），PS 和 IR 不用。
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "lux.h"
#include "log.h"

#define AP3216C_REG_SYS_CONF	0x00
#define AP3216C_REG_ALS_L	0x0C

/* 修改：写一个寄存器，reg + value 一次事务，等价于 i2cset -y 0 0x1e reg val */
static int lux_write_reg(lux_t *lx, unsigned char reg, unsigned char val)
{
	unsigned char buf[2];

	buf[0] = reg;
	buf[1] = val;

	if (write(lx->fd, buf, 2) != 2) {
		LOG_WARN("lux: write reg 0x%02x failed: %s", reg, strerror(errno));
		return -1;
	}

	return 0;
}

int lux_open(lux_t *lx, const char *dev, int addr)
{
	if (lx == NULL)
		return -1;

	memset(lx, 0, sizeof(*lx));
	lx->fd = -1;

	if (dev == NULL || dev[0] == '\0')
		dev = LUX_DEFAULT_DEV;
	if (addr <= 0)
		addr = LUX_DEFAULT_ADDR;

	lx->fd = open(dev, O_RDWR);
	if (lx->fd < 0) {
		LOG_WARN("lux: open %s failed: %s", dev, strerror(errno));
		return -1;
	}

	/*
	 * 认领地址。驱动加载后这里会返回 EBUSY：
	 * 那是 C 阶段的事，到时候加 sysfs 分支。
	 */
	if (ioctl(lx->fd, I2C_SLAVE, addr) < 0) {
		LOG_WARN("lux: I2C_SLAVE 0x%02x failed: %s%s", addr,
		         strerror(errno),
		         (errno == EBUSY) ? " (kernel driver owns this address)" : "");
		close(lx->fd);
		lx->fd = -1;
		return -1;
	}

	/* 和 A 阶段的 i2cset 0x00 0x03 一致：ALS + PS + IR 都使能 */
	if (lux_write_reg(lx, AP3216C_REG_SYS_CONF, 0x03) < 0) {
		close(lx->fd);
		lx->fd = -1;
		return -1;
	}

	lx->addr = addr;
	usleep(20 * 1000);	/* 刚使能时 ALS 还没稳定，等一下再读 */

	LOG_INFO("lux: %s addr 0x%02x", dev, addr);

	return 0;
}

int lux_read(lux_t *lx, int *lux)
{
	unsigned char reg = AP3216C_REG_ALS_L;
	unsigned char buf[2];
	int als;

	if (lx == NULL || lx->fd < 0 || lux == NULL)
		return -1;

	/*
	 * 先把寄存器指针指到 0x0C，再连读两个字节：
	 * AP3216C 读的时候地址自增，所以第二个字节就是 0x0D。
	 */
	if (write(lx->fd, &reg, 1) != 1) {
		LOG_WARN("lux: set reg 0x%02x failed: %s", reg, strerror(errno));
		return -1;
	}

	if (read(lx->fd, buf, 2) != 2) {
		LOG_WARN("lux: read ALS failed: %s", strerror(errno));
		return -1;
	}

	/* 高字节 bit7 是 ALS 有效标志，数据在低 7 位 */
	als = ((buf[1] & 0x7F) << 8) | buf[0];

	/* lux = ALS * 0.35，整数实现，避免浮点 */
	*lux = als * 35 / 100;

	return 0;
}

void lux_close(lux_t *lx)
{
	if (lx == NULL || lx->fd < 0)
		return;

	close(lx->fd);
	lx->fd = -1;
}
