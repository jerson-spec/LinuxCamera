/*
 * 修改：新增背光控制模块。
 *
 * 只做三件事：读 max_brightness、写 brightness、退出时把原值还回去。
 * 对外用百分比（0~100），内部换算成 0..max 的档位：
 * 本板 max_brightness=8，一档就是 12.5%，所以滑动按"档"走，
 * 不会出现"划了两下画面没变"的情况。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "backlight.h"
#include "log.h"

/* 修改：sysfs 属性每次 write 前都要 lseek 回 0，否则第二次写会失败 */
static int bl_write_value(int fd, int value)
{
	char buf[16];
	int len, off;
	int n = 0;

	len = snprintf(buf, sizeof(buf), "%d\n", value);
	if (len <= 0)
		return -1;

	if (lseek(fd, 0, SEEK_SET) < 0)
		return -1;

	for (off = 0; off < len; off += n) {
		n = (int)write(fd, buf + off, (size_t)(len - off));
		if (n < 0) {
			if (errno == EINTR) {
				n = 0;
				continue;
			}
			return -1;
		}
		if (n == 0)
			return -1;
	}

	return 0;
}

static int bl_read_value(const char *dir, const char *name, int *out)
{
	char path[160];
	char buf[32];
	int fd, n;

	snprintf(path, sizeof(path), "%s/%s", dir, name);

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

/* 修改：百分比 -> 档位。四舍五入，再夹到 [min_level, max] */
static int bl_level_from_percent(const backlight_t *bl, int percent)
{
	int level;

	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;

	level = (bl->max * percent + 50) / 100;

	if (level < bl->min_level)
		level = bl->min_level;
	if (level > bl->max)
		level = bl->max;

	return level;
}

/* 修改：真正写下去，并读回 actual_brightness 确认驱动收下了 */
static int bl_apply(backlight_t *bl, int level)
{
	int actual;

	if (bl->fd < 0)
		return -1;

	if (level < bl->min_level)
		level = bl->min_level;
	if (level > bl->max)
		level = bl->max;

	if (bl_write_value(bl->fd, level) < 0) {
		LOG_WARN("backlight: write %d failed: %s", level, strerror(errno));
		return -1;
	}

	if (bl_read_value(bl->dir, "actual_brightness", &actual) == 0 &&
	    actual != level)
		LOG_WARN("backlight: want %d, actual %d", level, actual);

	bl->level = level;

	return 0;
}

int backlight_init(backlight_t *bl, const char *dir, int percent)
{
	char path[160];

	if (bl == NULL)
		return -1;

	memset(bl, 0, sizeof(*bl));
	bl->fd = -1;

	if (dir == NULL || dir[0] == '\0')
		dir = BACKLIGHT_DEFAULT_DIR;

	snprintf(bl->dir, sizeof(bl->dir), "%s", dir);

	if (bl_read_value(bl->dir, "max_brightness", &bl->max) < 0 || bl->max <= 0) {
		LOG_WARN("backlight: no usable max_brightness under %s", bl->dir);
		return -1;
	}

	bl->min_level = bl->max * BACKLIGHT_MIN_PERCENT / 100;
	if (bl->min_level < 1)
		bl->min_level = 1;

	/* 修改：记下启动前的档位，退出时还回去 */
	if (bl_read_value(bl->dir, "brightness", &bl->orig) < 0)
		bl->orig = bl->max;

	snprintf(path, sizeof(path), "%s/brightness", bl->dir);
	bl->fd = open(path, O_RDWR);
	if (bl->fd < 0) {
		LOG_WARN("backlight: open %s failed: %s", path, strerror(errno));
		return -1;
	}

	if (percent < 0)
		percent = BACKLIGHT_DEFAULT_PERCENT;

	if (bl_apply(bl, bl_level_from_percent(bl, percent)) < 0) {
		close(bl->fd);
		bl->fd = -1;
		return -1;
	}

	LOG_INFO("backlight: %s max=%d orig=%d now=%d%%",
	         bl->dir, bl->max, bl->orig, backlight_percent(bl));

	return 0;
}

int backlight_set(backlight_t *bl, int percent)
{
	if (bl == NULL || bl->fd < 0)
		return -1;

	return bl_apply(bl, bl_level_from_percent(bl, percent));
}

/* 修改：按档步进，delta 是 +1 / -1，滑动一次走一档 */
int backlight_step(backlight_t *bl, int delta)
{
	if (bl == NULL || bl->fd < 0)
		return -1;

	return bl_apply(bl, bl->level + delta);
}

int backlight_percent(const backlight_t *bl)
{
	if (bl == NULL || bl->max <= 0)
		return -1;

	return (bl->level * 100 + bl->max / 2) / bl->max;
}

/* 修改：给自动亮度用的档位接口 */
int backlight_max_level(const backlight_t *bl)
{
	return (bl == NULL) ? 0 : bl->max;
}

int backlight_level(const backlight_t *bl)
{
	return (bl == NULL) ? -1 : bl->level;
}

int backlight_set_level(backlight_t *bl, int level)
{
	if (bl == NULL || bl->fd < 0)
		return -1;

	return bl_apply(bl, level);
}

void backlight_deinit(backlight_t *bl)
{
	if (bl == NULL || bl->fd < 0)
		return;

	/* 修改：把启动前的档位还回去，不让板子停在调过的亮度上 */
	if (bl->orig > 0 && bl->orig != bl->level)
		(void)bl_write_value(bl->fd, bl->orig);

	close(bl->fd);
	bl->fd = -1;
}
