#ifndef _BACKLIGHT_H
#define _BACKLIGHT_H

/* 修改：新增背光模块。默认路径就是板上 /sys/class/backlight/backlight */
#define BACKLIGHT_DEFAULT_DIR      "/sys/class/backlight/backlight"

/* 修改：上电默认亮度。80% -> max=8 时落在第 6 档 */
#define BACKLIGHT_DEFAULT_PERCENT  80

/* 修改：最低档 */
#define BACKLIGHT_MIN_PERCENT      10

typedef struct {
	int  fd;              /* 打开的 brightness 节点，-1 表示没打开 */
	int  max;             /* max_brightness，本板是 8 */
	int  min_level;       /* 对应 BACKLIGHT_MIN_PERCENT 的档位，本板是 1 */
	int  level;           /* 当前档位 0..max */
	int  orig;            /* 启动前的档位，deinit 时恢复 */
	char dir[128];        /* /sys/class/backlight/backlight */
} backlight_t;

int  backlight_init(backlight_t *bl, const char *dir, int percent);
int  backlight_set(backlight_t *bl, int percent);
int  backlight_step(backlight_t *bl, int delta);
int  backlight_percent(const backlight_t *bl);
void backlight_deinit(backlight_t *bl);

/* 修改：自动亮度按档位运算，避免百分比量化来回抖 */
int  backlight_max_level(const backlight_t *bl);
int  backlight_level(const backlight_t *bl);
int  backlight_set_level(backlight_t *bl, int level);

#endif
