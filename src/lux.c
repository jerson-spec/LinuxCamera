/*
 * 修改：光感读取模块（AP3216C）。
 *
 * 两条后端，open 时自动选：
 *   1) 内核驱动已加载 -> /sys/bus/iio/devices/iio:deviceN/in_illuminance_input
 *   2) 没有驱动      -> /dev/i2c-0 直读 0x0C/0x0D
 *
 * 因为 i2c-dev 在驱动占用地址后返回 EBUSY，两条路不能同时走。
 * 选哪条是 open 时决定的，切换后端要重启程序。
 *
 * 直读路径的寄存器口径和 A 阶段 i2cset/i2cget 一致：
 *   0x00 写 0x03 使能，0x0C/0x0D 读 ALS 低/高字节。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <time.h>

#include "lux.h"
#include "log.h"

#define AP3216C_REG_SYS_CONF	0x00
#define AP3216C_REG_ALS_L	0x0C

#define IIO_DEV_DIR	"/sys/bus/iio/devices"
#define IIO_LIGHT_ATTR	"in_illuminance_input"
#define IIO_NAME_ATTR	"name"
#define IIO_NAME	"ap3216c"

static void sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

/* 修改：读一个 sysfs 属性并转成 int，形如 "320\n" */
static int read_file_int(const char *path, int *out)
{
	char buf[32];
	int fd, n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	n = (int)read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;

	buf[n] = '\0';
	*out = atoi(buf);

	return 0;
}

/*
 * 修改：在 /sys/bus/iio/devices 里找名字是 ap3216c 的设备。
 * 不能写死 iio:device0 —— 设备编号随加载顺序变。
 */
static int find_iio(char *out, size_t cap)
{
	DIR *d;
	struct dirent *de;

	d = opendir(IIO_DEV_DIR);
	if (d == NULL)
		return -1;

	while ((de = readdir(d)) != NULL) {
		char name_path[300];
		char name[32];
		int fd, n;

		if (strncmp(de->d_name, "iio:device", 10) != 0)
			continue;

		snprintf(name_path, sizeof(name_path), "%s/%s/%s",
		         IIO_DEV_DIR, de->d_name, IIO_NAME_ATTR);

		fd = open(name_path, O_RDONLY);
		if (fd < 0)
			continue;

		n = (int)read(fd, name, sizeof(name) - 1);
		close(fd);
		if (n <= 0)
			continue;

		name[n] = '\0';
		name[strcspn(name, "\n")] = '\0';

		if (strcmp(name, IIO_NAME) != 0)
			continue;

		snprintf(out, cap, "%s/%s/%s",
		         IIO_DEV_DIR, de->d_name, IIO_LIGHT_ATTR);
		closedir(d);
		return 0;
	}

	closedir(d);
	return -1;
}

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

	/*
	 * 修改：优先走驱动。驱动在的话 i2c-dev 那边是抢不到的，
	 * 所以先找 sysfs；读一下确认属性真的能用，防止名字对上但节点是坏的。
	 */
	if (find_iio(lx->path, sizeof(lx->path)) == 0) {
		int v;

		if (read_file_int(lx->path, &v) == 0) {
			lx->iio = 1;
			lx->addr = addr;
			LOG_INFO("lux: %s", lx->path);
			return 0;
		}
		LOG_WARN("lux: %s not readable, fallback to i2c-dev", lx->path);
	}

	lx->fd = open(dev, O_RDWR);
	if (lx->fd < 0) {
		LOG_WARN("lux: open %s failed: %s", dev, strerror(errno));
		return -1;
	}

	if (ioctl(lx->fd, I2C_SLAVE, addr) < 0) {
		/*
		 * EBUSY = 内核驱动占着这个地址。
		 * 正常情况下上面 sysfs 分支就该走通；走不通又 EBUSY，
		 * 说明驱动加载了但 IIO 节点没建出来。
		 */
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
	sleep_ms(20);	/* 刚使能时 ALS 还没稳定，等一下再读 */

	LOG_INFO("lux: %s addr 0x%02x (i2c-dev)", dev, addr);

	return 0;
}

static int lux_read_iio(lux_t *lx, int *lux)
{
	/*
	 * 每次 open/read/close：sysfs 读一次就触发一次 I2C，
	 * 200ms 一次的开销可以忽略，省得管文件偏移。
	 */
	return read_file_int(lx->path, lux);
}

static int lux_read_i2c(lux_t *lx, int *lux)
{
	unsigned char reg = AP3216C_REG_ALS_L;
	unsigned char buf[2];
	int als;

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

int lux_read(lux_t *lx, int *lux)
{
	if (lx == NULL || lux == NULL)
		return -1;

	/* 修改：两条后端在这里分叉，调用方完全不用知道 */
	if (lx->iio)
		return lux_read_iio(lx, lux);

	if (lx->fd < 0)
		return -1;

	return lux_read_i2c(lx, lux);
}

void lux_close(lux_t *lx)
{
	if (lx == NULL)
		return;

	if (lx->fd >= 0) {
		close(lx->fd);
		lx->fd = -1;
	}

	lx->iio = 0;
}
