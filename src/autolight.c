/*
 * 修改：自动亮度策略。
 *
 * 三件事：lux 映射成档位、连续几次一致才认、认了以后一档一档走过去。
 *
 * 阈值按板上实测定的：
 *   全黑 0       -> 20%
 *   室内 80-90   -> 80%（和默认亮度一致）
 *   手电 1000    -> 100%
 */
#include <stddef.h>	/* 修改：NULL */

#include "autolight.h"

/* lux 低于阈值就取对应百分比；最后一项给 >=500 的情况 */
static const int lux_thr[] = { 5, 30, 120, 500 };
static const int lux_pct[] = { 20, 40, 80, 90, 100 };

static int lux_to_level(int lux, int max_level)
{
	int i, pct, level;
	int n = (int)(sizeof(lux_thr) / sizeof(lux_thr[0]));

	if (max_level <= 0)
		return 0;

	if (lux < 0)
		lux = 0;

	for (i = 0; i < n; i++) {
		if (lux < lux_thr[i])
			break;
	}

	pct = lux_pct[i];
	if (pct > AUTOLIGHT_MAX_PERCENT)
		pct = AUTOLIGHT_MAX_PERCENT;

	/* 四舍五入：max=8 时 20% 应该落在 2 档(25%)，截断会掉到 1 档(12%) */
	level = (max_level * pct + 50) / 100;

	if (level < 1)
		level = 1;
	if (level > max_level)
		level = max_level;

	return level;
}

void autolight_init(autolight_t *al, int cur_level, long long now_ms)
{
	if (al == NULL)
		return;

	al->target_level = -1;
	al->pending_level = -1;
	al->pending_cnt = 0;
	al->cur_level = cur_level;
	al->manual_until_ms = 0;
	al->grace_until_ms = now_ms + AUTOLIGHT_GRACE_MS;
}

int autolight_update(autolight_t *al, int lux, int max_level, long long now_ms)
{
	int level, step;

	if (al == NULL)
		return -1;

	/* 上电头 1 秒 ALS 还不可信，先不动 */
	if (now_ms < al->grace_until_ms)
		return -1;

	/* 用户刚滑过，别跟他抢 */
	if (now_ms < al->manual_until_ms)
		return -1;

	level = lux_to_level(lux, max_level);

	/* 迟滞：连续 AUTOLIGHT_STABLE_CNT 次落在同一档才算数 */
	if (level == al->target_level) {
		al->pending_cnt = 0;
	} else if (level == al->pending_level) {
		if (++al->pending_cnt >= AUTOLIGHT_STABLE_CNT) {
			al->target_level = level;
			al->pending_cnt = 0;
		}
	} else {
		al->pending_level = level;
		al->pending_cnt = 1;
	}

	if (al->target_level < 0 || al->cur_level == al->target_level)
		return -1;

	/* 一次只走一档，避免亮度瞬间跳变 */
	step = (al->target_level > al->cur_level) ? 1 : -1;
	al->cur_level += step;

	return al->cur_level;
}

void autolight_manual(autolight_t *al, long long now_ms)
{
	if (al == NULL)
		return;

	al->manual_until_ms = now_ms + AUTOLIGHT_MANUAL_MS;
	al->pending_cnt = 0;
}

void autolight_sync(autolight_t *al, int level)
{
	if (al == NULL)
		return;

	al->cur_level = level;
}
