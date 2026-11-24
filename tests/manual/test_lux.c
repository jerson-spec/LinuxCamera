/*
 * 修改：光感直读的小测试程序。
 * 不接摄像头、不开 LCD，只验证 lux_open/lux_read 这条链路。
 *
 *   ./test_lux [设备] [地址]
 *   ./test_lux                      # 默认 /dev/i2c-0 0x1e
 *   ./test_lux /dev/i2c-0 0x1e
 *
 * 手遮住传感器数值应下降，手电照应上升。Ctrl-C 退出。
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "lux.h"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
	(void)sig;
	g_stop = 1;
}

int main(int argc, char **argv)
{
	lux_t lx;
	const char *dev;
	int addr;
	int n = 0;

	dev = (argc > 1) ? argv[1] : LUX_DEFAULT_DEV;
	addr = (argc > 2) ? (int)strtol(argv[2], NULL, 0) : LUX_DEFAULT_ADDR;

	signal(SIGINT, on_sigint);

	if (lux_open(&lx, dev, addr) < 0) {
		fprintf(stderr, "lux_open %s 0x%02x failed\n", dev, addr);
		return -1;
	}

	while (!g_stop) {
		int lux;

		if (lux_read(&lx, &lux) == 0) {
			printf("lux: %d\n", lux);
			n++;
		} else {
			printf("lux: read error\n");
		}

		fflush(stdout);
		usleep(200 * 1000);	/* 5Hz，和主程序采样周期一致 */
	}

	printf("exit, %d samples\n", n);
	lux_close(&lx);

	return 0;
}
