#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>

#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <strings.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

#include "config.h"
#include "lcd.h"
#include "yuyv.h"
#include "log.h"
#include "camera.h"

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_save = 0;

/* 返回当前进程的 VmRSS，单位 kB；失败返回 -1 */
static long get_vmrss_kb(void)
{
	FILE *fp = fopen("/proc/self/status", "r");
	char line[256];
	long kb = -1;

	if (fp == NULL)
		return -1;

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "VmRSS:", 6) == 0) {
			sscanf(line + 6, "%ld", &kb);
			break;
		}
	}
	fclose(fp);

	return kb;
}

/* 单调时钟，单位 ms */
static double get_time_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		LOG_ERROR("clock_gettime");
		return 0.0;
	}

	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void on_sigint(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void on_sigusr1(int sig)
{
	(void)sig;
	g_save = 1;
}

/* 把一段内存原样写到文件；处理部分写和 EINTR */
static int snapshot_save(const char *path, const unsigned char *data, size_t len)
{
	size_t off = 0;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		LOG_ERROR("open snapshot %s: %s", path, strerror(errno));
		return -1;
	}

	while (off < len) {
		ssize_t n = write(fd, data + off, len - off);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			LOG_ERROR("write snapshot %s: %s", path, strerror(errno));
			close(fd);
			return -1;
		}
		off += (size_t)n;
	}

	close(fd);
	return 0;
}

/*
 * 返回值：
 *   0  拍照成功，预览已恢复
 *   1  拍照失败，但预览已恢复（可以继续跑）
 *  -1  预览恢复失败（必须退出）
 */
static int snapshot_mjpeg(camera_t *cam,const app_config_t *cfg,const char *path,size_t *out_len)
{
	struct v4l2_buffer buf;
	struct pollfd pfd;
	size_t n;
	int ret = 1;
	
	/* camera_stop */
	camera_stop(cam);
	
	/* set format: jpeg */
	if(camera_set_format(cam, (int)cam->width,(int)cam->height, V4L2_PIX_FMT_MJPEG) < 0){
		goto restore;
	}
	/* camera_start */
	if(camera_start(cam, cfg->buffers) < 0){
		goto restore;
	}	

	pfd.fd = cam->fd;
	pfd.events = POLLIN;
	if(poll(&pfd , 1 , 2000) <= 0){
		LOG_ERROR("snapshot: poll timeout");
		goto stop;
	}
	
	/* camera_dequeue */
	if(camera_dequeue(cam, &buf) < 0){
		goto stop;
	}
	
	n = buf.bytesused;
	if(n == 0){
		LOG_ERROR("snapshot: empty frame");
		camera_queue(cam, &buf);
		goto stop;
	}

	/* 处理 */
	if(snapshot_save(path, cam->mem[buf.index], n )< 0){
		camera_queue(cam,&buf);
		goto stop;
	}
	camera_queue(cam,&buf);
	*out_len = n;
	ret = 0;
	
stop:
	camera_stop(cam);

restore:
	if(camera_set_format(cam, (int)cam->width,(int)cam->height, V4L2_PIX_FMT_YUYV) < 0 ){
		ret = -1;
		goto restore_fail;
	}
		
	camera_set_fps_if_needed(cam, 1, 30);

	if(camera_start(cam, cfg->buffers) < 0){
		ret = -1;
		goto restore_fail;
	}
restore_fail:
	if(ret == -1)
		LOG_ERROR("snapshot: failed to restore preview");
	
	return ret;
}


/*
 *	./test_camera_preview [path/camera.ini] [path/lcd.ini]
 */
int main(int argc, char **argv)
{
	int ret;
	int i;
	int exit_code = 0;
	int lcd_ok = 0;
	int poll_timeout_cnt = 0;
	int x0 = 0;
	int y0 = 0;

	struct fb_var_screeninfo lcd_var_screeninfo;
	struct pollfd fds[1];
	struct sigaction sa;
	struct timespec ts;
	struct tm tm;

	camera_t cam;
	unsigned char *rgbbuf = NULL;
	size_t size = 0;
	size_t shot_len = 0;
	unsigned int pixfmt;

	app_config_t camera_cfg;
	app_config_t lcd_cfg;
	char *camera_cfg_path;
	char *lcd_cfg_path;

	unsigned int dq, q, frames;
	unsigned int last_sequence, have_sequence, dropped;
	double t0, t1, t2, t3, last_t0;
	double wait_time, conv_time, draw_time, n;

	char tbuf[32];
	char filename[512];
	const char *ext = "jpg";	/* 目前写的是 RGB888 裸数据，等 MJPEG 拍照接入后改成 jpg */

	memset(&cam, 0, sizeof(cam));
	cam.fd = -1;
	memset(&camera_cfg, 0, sizeof(camera_cfg));
	memset(&lcd_cfg, 0, sizeof(lcd_cfg));

	dq = q = frames = 0;
	last_sequence = have_sequence = dropped = 0;
	last_t0 = wait_time = conv_time = draw_time = n = 0.0;

	if (argc > 3) {
		fprintf(stderr, "Usage:%s [path/camera.ini] [path/lcd.ini]\n", argv[0]);
		return -1;
	}
	camera_cfg_path = (argc > 1) ? argv[1] : "config/camera.ini";
	lcd_cfg_path   = (argc > 2) ? argv[2] : "config/lcd.ini";

	/* 1. 相机配置（相机模块不再负责解析 ini） */
	if (config_load(camera_cfg_path, &camera_cfg) < 0) {
		fprintf(stderr, "can't load camera config: %s\n", camera_cfg_path);
		return -1;
	}
	config_print(&camera_cfg);

	if (mkdir(camera_cfg.save_dir, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "mkdir %s: %s\n", camera_cfg.save_dir, strerror(errno));
		return -1;
	}

	/* 2. LCD（注意：lcd_init 内部会把日志切到 lcd.log） */
	if (lcd_init(lcd_cfg_path, &lcd_cfg) < 0) {
		fprintf(stderr, "lcd_init failed\n");
		return -1;
	}
	lcd_ok = 1;
	Is_full_range(&lcd_cfg);
	Get_lcd_parameters(NULL, &lcd_var_screeninfo);
	config_print(&lcd_cfg);

	/* 3. 日志按 camera.ini 再初始化一次，之后走 camera.log */
	{
		log_level_t log_level;

		if (log_level_parse(camera_cfg.log_level, &log_level) < 0) {
			LOG_ERROR("bad log level: %s", camera_cfg.log_level);
		} else if (log_init(log_level, camera_cfg.log_file) < 0) {
			LOG_ERROR("log_init %s failed: %s", camera_cfg.log_file, strerror(errno));
		}
	}

	/* 4. 相机：打开、设格式、请求帧率 */
	pixfmt = camera_pixfmt_from_string(camera_cfg.format);
	if (pixfmt == 0) {
		LOG_ERROR("unsupported format: %s", camera_cfg.format);
		exit_code = -1;
		goto cleanup;
	}
	if (camera_open(&cam, camera_cfg.device) < 0) {
		exit_code = -1;
		goto cleanup;
	}
	if (camera_set_format(&cam, camera_cfg.width, camera_cfg.height, pixfmt) < 0) {
		exit_code = -1;
		goto cleanup;
	}
	if (cam.width != lcd_var_screeninfo.xres || cam.height != lcd_var_screeninfo.yres) {
		LOG_WARN("camera %ux%u != lcd %ux%u, letterbox enabled",
		         cam.width, cam.height,
		         lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
	}
	(void)camera_set_fps(&cam, 1, 30);	/* 尽力而为，失败不影响预览 */

	/* 居中偏移只和分辨率、屏幕有关，算一次就够 */
	x0 = ((int)lcd_var_screeninfo.xres - (int)cam.width) / 2;
	y0 = ((int)lcd_var_screeninfo.yres - (int)cam.height) / 2;
	if (x0 < 0 || y0 < 0) {
		LOG_ERROR("camera %ux%u larger than screen %ux%u",
		          cam.width, cam.height,
		          lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
		exit_code = -1;
		goto cleanup;
	}

	/* 5. 信号 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sigusr1;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		LOG_ERROR("sigaction SIGUSR1: %s", strerror(errno));
		exit_code = -1;
		goto cleanup;
	}
	sa.sa_handler = on_sigint;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		LOG_ERROR("sigaction SIGINT: %s", strerror(errno));
		exit_code = -1;
		goto cleanup;
	}

	/* 6. RGB 缓冲 */
	size = (size_t)cam.width * (size_t)cam.height * 3;
	rgbbuf = malloc(size);
	if (rgbbuf == NULL) {
		LOG_ERROR("malloc rgb buffer (%lu bytes): %s",
		          (unsigned long)size, strerror(errno));
		exit_code = -1;
		goto cleanup;
	}
	memset(rgbbuf, 0, size);

	/* 7. 起流 */
	if (camera_start(&cam, camera_cfg.buffers) < 0) {
		exit_code = -1;
		goto cleanup;
	}

	fds[0].fd = cam.fd;
	fds[0].events = POLLIN;

	last_t0 = get_time_ms();
	int timeout_ms = (frames == 0)? 5000 : 2000; 
	while (!g_stop) {
		fds[0].revents = 0;
		t0 = get_time_ms();

		ret = poll(fds, 1, timeout_ms);
		if (ret == 0) {
			LOG_WARN("poll timeout");
			poll_timeout_cnt++;
			if (poll_timeout_cnt >= 3) {
				LOG_ERROR("poll timeout: %d times!", poll_timeout_cnt);
				exit_code = -1;
				break;
			}
			continue;
		} else if (ret < 0) {
			if (errno == EINTR)
				continue;
			LOG_ERROR("poll: %s", strerror(errno));
			exit_code = -2;
			break;
		}

		if (!(fds[0].revents & POLLIN)) {
			if (fds[0].revents & POLLERR || fds[0].revents & POLLHUP) {
				LOG_ERROR("poll exit");
				exit_code = -3;
				break;
			}
			LOG_WARN("poll revents mismatch: %hd", fds[0].revents);
			continue;
		}

		{
			struct v4l2_buffer buf;

			if (camera_dequeue(&cam, &buf) < 0) {
				exit_code = -4;
				break;
			}
			dq++;
			poll_timeout_cnt = 0;
			t1 = get_time_ms();

			/* 丢帧统计 */
			if (have_sequence && (buf.sequence - last_sequence) > 1)
				dropped += buf.sequence - last_sequence - 1;
			have_sequence = 1;
			last_sequence = buf.sequence;

			/* 短帧：还回去，丢这一帧 */
			if (buf.bytesused < (unsigned int)(cam.width * cam.height * 2)) {
				LOG_ERROR("short YUYV frame: %u < %d",
				          buf.bytesused, cam.width * cam.height * 2);
				if (camera_queue(&cam, &buf) < 0) {
					exit_code = -5;
					break;
				}
				q++;
				continue;
			}

			if (yuyv_to_rgb(cam.mem[buf.index], rgbbuf, cam.width, cam.height) < 0) {
				LOG_ERROR("yuyv_to_rgb failed");
				exit_code = -6;
				break;
			}
			t2 = get_time_ms();

			/* 尽早归还，驱动可以继续采 */
			if (camera_queue(&cam, &buf) < 0) {
				exit_code = -7;
				break;
			}
			frames++;
			q++;

			/* 3. 上屏（按行写，letterbox 居中） */
			for (i = 0; i < (int)cam.height; i++) {
				if (lcd_write_line_rgb888(y0 + i, x0,
				                          rgbbuf + (size_t)i * 3 * (size_t)cam.width,
				                          (int)cam.width) < 0) {
					LOG_ERROR("lcd_write_line_rgb888 failed at line %d", i);
					exit_code = -8;
					break;
				}
			}
			if (exit_code != 0)
				break;
		}

		t3 = get_time_ms();
		wait_time += t1 - t0;
		conv_time += t2 - t1;
		draw_time += t3 - t2;
		n++;

		/* 每秒统计 */
		if ((t3 - last_t0) >= 1000.0) {
			double dt = t3 - last_t0;

			LOG_INFO("fps: %.1f, wait: %.1f, conv: %.1f, draw: %.1f, dropped: %u, rss: %ld kB",
			         n * 1000.0 / dt, wait_time / n, conv_time / n, draw_time / n,
			         dropped, get_vmrss_kb());
			last_t0 = t3;
			wait_time = conv_time = draw_time = n = 0;
		}

		/* 拍照：触发源只是置标志，真正处理在循环里 */
		if (g_save) {
			int r;
			double t_shot_s = get_time_ms();

			clock_gettime(CLOCK_REALTIME, &ts);
			localtime_r(&ts.tv_sec, &tm);
			strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", &tm);
			snprintf(filename, sizeof(filename), "%s/IMG_%s.%s",
			         camera_cfg.save_dir, tbuf, ext);

			r = snapshot_mjpeg(&cam, &camera_cfg, filename, &shot_len);

		    /* 关键：重新起流后 buf.sequence 归零 */
		    have_sequence = 0;
		    poll_timeout_cnt = 0;

		    if (r < 0) {
		        exit_code = -9;
		        g_save = 0;
		        break;
		    }
		    if (r == 0)
		        LOG_INFO("snapshot: %s (%lu bytes, %.0f ms)",
		                 filename, (unsigned long)shot_len, get_time_ms() - t_shot_s);
		    else
		        LOG_WARN("snapshot failed, preview restored");
			
			g_save = 0;
		}

		/* 双缓冲切页 */
		if (lcd_doublebuffering_on() && lcd_flip() < 0)
			LOG_ERROR("lcd_flip failed, single buffer fallback");
	}

	LOG_INFO("DQbuf: %u, Qbuf: %u, Frames: %u", dq, q, frames);

cleanup:
	if (exit_code != 0)
		LOG_ERROR("exit_code: %d", exit_code);

	if (cam.fd >= 0)
		camera_close(&cam);
	free(rgbbuf);
	if (lcd_ok)
		lcd_deinit();

	return (exit_code != 0) ? -1 : 0;
}
