#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <string.h>

#include "touch.h"
#include "lcd.h"
#include "log.h"

/* 板子上 cat /proc/bus/input/devices 可查看输入设备信息 */
/* 找到触摸屏设备后可 hexdump /dev/input/eventX 实时查看触摸屏输出信息 */

/*
*	读取一个轴的绝对坐标范围
*	- 有的驱动对不支持的轴不返回错误，而是把 min/max 都填 0；
*	- 所以除了判断 ioctl 失败，还要判断 max > min。
*/
static int touch_abs_range(int fd, unsigned int axis, int *min, int *max)
{
	struct input_absinfo absI;

	memset(&absI, 0, sizeof(absI));
	if(ioctl(fd, EVIOCGABS(axis), &absI) < 0)
		return -1;

	if(absI.maximum <= absI.minimum)
		return -1;

	*min = absI.minimum;
	*max = absI.maximum;

	return 0;
}

int touch_open(touch_t *tch_p, const char*dev)
{
	int min, max;

	memset(tch_p, 0, sizeof(touch_t));
	tch_p->fd = -1;

	tch_p->fd = open(dev, O_RDONLY | O_NONBLOCK);
	if(tch_p->fd < 0){
		tch_p->fd = -1;
		LOG_ERROR("touch: open %s failed: %s", dev, strerror(errno));
		return -1;
	}

	/* X轴：先试单点 ABS_X，不行再试多点 ABS_MT_POSITION_X */
	if(touch_abs_range(tch_p->fd, ABS_X, &min, &max) == 0 ||
	   touch_abs_range(tch_p->fd, ABS_MT_POSITION_X, &min, &max) == 0){
		tch_p->x_min = min;
		tch_p->x_max = max;
	}
	else{
		LOG_ERROR("touch: no usable X axis on %s", dev);
		goto fail;
	}

	/* Y轴：同理 */
	if(touch_abs_range(tch_p->fd, ABS_Y, &min, &max) == 0 ||
	   touch_abs_range(tch_p->fd, ABS_MT_POSITION_Y, &min, &max) == 0){
		tch_p->y_min = min;
		tch_p->y_max = max;
	}
	else{
		LOG_ERROR("touch: no usable Y axis on %s", dev);
		goto fail;
	}

	LOG_INFO("touch: %s x[%d,%d] y[%d,%d]", dev,
	         tch_p->x_min, tch_p->x_max, tch_p->y_min, tch_p->y_max);

	return 0;

fail:
	close(tch_p->fd);
	tch_p->fd = -1;
	return -1;
}

void touch_set_range(touch_t *tch_p, int x_min, int x_max, int y_min, int y_max)
{
	if(x_max <= x_min || y_max <= y_min){
		LOG_WARN("touch: bad range x[%d,%d] y[%d,%d], keep absinfo",
		         x_min, x_max, y_min, y_max);
		return;
	}

	tch_p->x_min = x_min;
	tch_p->x_max = x_max;
	tch_p->y_min = y_min;
	tch_p->y_max = y_max;
}

int touch_read(touch_t *tch_p, int *x, int *y)
{
	struct input_event ev;
	ssize_t n;
	int tap = 0;
	struct fb_var_screeninfo vi;
	int xres, yres;

	memset(&vi, 0, sizeof(vi));
	Get_lcd_parameters(NULL, &vi);
	xres = (int)vi.xres;
	yres = (int)vi.yres;

	/* 一次把队列里的事件全部消费掉，读到 EAGAIN 为止 */
	while ((n = read(tch_p->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
		if (ev.type == EV_ABS) {
			if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
				tch_p->x_val = ev.value;
				tch_p->have_xy = 1;
			} else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
				tch_p->y_val = ev.value;
				tch_p->have_xy = 1;
			}
		} else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
			if (ev.value) {
				tch_p->pressed = 1;
			} else {
				tch_p->pressed = 0;
				if (tch_p->have_xy)
					tap = 1;          /* 抬起且坐标有效 = 一次点击 */
			}
		}
	}

	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		return -1;

	if (!tap)
		return 0;                     /* 没有点击 */

	/* 范围不合法时不做映射，把坐标置 -1，调用方就不会按坐标分区 */
	if (tch_p->x_max > tch_p->x_min && tch_p->y_max > tch_p->y_min) {
		int mx = (tch_p->x_val - tch_p->x_min) * xres / (tch_p->x_max - tch_p->x_min);
		int my = (tch_p->y_val - tch_p->y_min) * yres / (tch_p->y_max - tch_p->y_min);

		/* 原始值偶尔会超出驱动报的范围，裁到屏幕内 */
		if (mx < 0) mx = 0;
		if (mx > xres - 1) mx = xres - 1;
		if (my < 0) my = 0;
		if (my > yres - 1) my = yres - 1;

		*x = mx;
		*y = my;
	} else {
		*x = -1;
		*y = -1;
	}

	tch_p->have_xy = 0;
	return 1;                         /* 1 = 有点击，把坐标带出去 */
}


void touch_close(touch_t *tch_p)
{
	if(tch_p->fd >= 0){
		close(tch_p->fd);
		tch_p->fd = -1;
	}
}
