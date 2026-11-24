#ifndef _TOUCH_H
#define _TOUCH_H

/*
 *	修改：手势类型。
 *	touch_read() 返回 1 时，tch_p->gesture 告诉调用方是哪一种动作，
 *	调用方先看手势，再决定要不要用 x/y 做分区。
 */
#define TOUCH_GESTURE_NONE        0
#define TOUCH_GESTURE_TAP         1
#define TOUCH_GESTURE_SWIPE_UP    2
#define TOUCH_GESTURE_SWIPE_DOWN  3

typedef struct{
	int fd;
	int x_val,x_max,x_min;
	int y_val,y_max,y_min;
	int pressed;
	int have_xy;

	/* 修改：手势判定用，记的是原始坐标，没做屏幕范围映射 */
	int gesture;
	int down_x,down_y;
	long long down_ms;
	int down_valid;      /* 本次触摸有没有记到起点 */
	int waiting_down;    /* 按下后还没等到 SYN_REPORT 落起点 */
}touch_t;

int touch_open(touch_t *tch_p, const char*dev);

/*
*	覆盖归一化范围
*	- 驱动报的 EVIOCGABS 范围可能和它实际发送的坐标不一致（屏幕换了、DT没改）；
*	- 用 evtest 摸一下四角，把实际看到的 min/max 填进来。
*/
void touch_set_range(touch_t *tch_p, int x_min, int x_max, int y_min, int y_max);

int touch_read(touch_t *tch_p, int *x ,int *y);

void touch_flush(touch_t *tch_p);

void touch_close(touch_t *tch_p);

#endif
