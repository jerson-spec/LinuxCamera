#ifndef _TOUCH_H
#define _TOUCH_H

typedef struct{
	int fd;
	int x_val,x_max,x_min;
	int y_val,y_max,y_min;
	int pressed;
	int have_xy;
}touch_t;

int touch_open(touch_t *tch_p, const char*dev);

/*
*	覆盖归一化范围
*	- 驱动报的 EVIOCGABS 范围可能和它实际发送的坐标不一致（屏幕换了、DT没改）；
*	- 用 evtest 摸一下四角，把实际看到的 min/max 填进来。
*/
void touch_set_range(touch_t *tch_p, int x_min, int x_max, int y_min, int y_max);

int touch_read(touch_t *tch_p, int *x ,int *y);

void touch_close(touch_t *tch_p);

#endif