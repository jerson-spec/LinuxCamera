#ifndef _AUTOLIGHT_H
#define _AUTOLIGHT_H

/*
 * 修改：自动亮度策略。
 *
 * 策略放用户态：驱动/用户态只负责把 lux 读出来，
 * 阈值、迟滞、渐变、手动优先都在这里。
 * 纯计算，不碰 I2C、不碰 sysfs、不打日志，方便脱开板子做单元测试。
 */

#define AUTOLIGHT_SAMPLE_MS     200     /* 采样周期，和 test_lux 一致 */
#define AUTOLIGHT_STABLE_CNT    3       /* 连续几次一致才认，3 次 = 600ms */
#define AUTOLIGHT_MANUAL_MS     30000   /* 手动滑动后暂停自动 30 秒 */
#define AUTOLIGHT_GRACE_MS      1000    /* 上电前 1 秒不动作，避开 ALS 未稳定 */

/*
 * 自动模式亮度上限。
 * 默认 100，如果实测出现"背光越亮 -> lux 越高 -> 再调亮"的正反馈，
 * 把这个改成 90 就能压住，手动滑动仍然可以到 100。
 */
#define AUTOLIGHT_MAX_PERCENT   100

typedef struct {
	int target_level;           /* 稳定后的目标档位，-1 表示还没定 */
	int pending_level;          /* 正在确认的候选档位 */
	int pending_cnt;            /* 候选连续出现几次 */
	int cur_level;              /* 当前档位 */
	long long manual_until_ms;  /* 手动覆盖截止时间 */
	long long grace_until_ms;   /* 启动宽限截止时间 */
} autolight_t;

/* now_ms 用单调时钟毫秒，主循环里就是 get_time_ms() */
void autolight_init(autolight_t *al, int cur_level, long long now_ms);

/*
 * 返回 -1：这次不改
 * 返回 >=0：新档位，调用方 set_level 就行
 * 每次最多移动一档，剩下的交给下一次采样。
 */
int  autolight_update(autolight_t *al, int lux, int max_level, long long now_ms);

/* 用户滑动后调用，暂停自动调整一段时间 */
void autolight_manual(autolight_t *al, long long now_ms);

/* 手动改过亮度后，把内部档位同步过来 */
void autolight_sync(autolight_t *al, int level);

#endif
