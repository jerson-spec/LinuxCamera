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
#include <dirent.h>
#include <linux/input.h>

#include "config.h"
#include "lcd.h"
#include "yuyv.h"
#include "log.h"
#include "camera.h"
#include "osd.h"
#include "touch.h"
#include "http.h"
#include "snapshot.h"
#include "backlight.h"	/* 修改：背光模块 */

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_save = 0;
static volatile sig_atomic_t g_res = 0;
/* 修改：上下滑的亮度增减，+1 加一档，-1 减一档 */
static volatile sig_atomic_t g_bright = 0;

typedef struct {
	unsigned int width;
	unsigned int height;
}res_switch_t;

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

static void on_sigusr2(int sig)
{
	(void)sig;
	g_res = 1;
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
 *   0  转换成功，预览已恢复
 *   1  转换失败，但预览已恢复（可以继续跑）
 *  -1  预览恢复失败（必须退出）
 */
static int res_switch(camera_t *cam,const app_config_t *cfg,res_switch_t *res_switch_p,
			unsigned char **rgbbuf_p,letterbox_t *lb_p)
{
	int ret = 1;
	size_t size;
	struct pollfd pfd;
	struct fb_var_screeninfo lcd_var_screeninfo;
	unsigned char* newbuf = NULL;
	
	unsigned int width  = res_switch_p->width;
	unsigned int height = res_switch_p->height;
	
	unsigned int restore_width  = cam->width;
	unsigned int restore_height = cam->height;

	if (cam->width == width && cam->height == height)
			return 0;

	Get_lcd_parameters(NULL, &lcd_var_screeninfo);

	if ((int)width > (int)lcd_var_screeninfo.xres || (int)height > (int)lcd_var_screeninfo.yres) {
        LOG_ERROR("res_switch: %ux%u larger than screen %ux%u",
                  width, height, lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
        return 1;
    }

	/* camera_stop */
	camera_stop(cam);

	/* set format: width * height */
	if(camera_set_format(cam, (int)width,(int)height, cam->pixfmt) < 0){
		goto restore;
	}
		
	if (lb_p->x_offset < 0 || lb_p->y_offset < 0) {
		LOG_ERROR("camera %ux%u larger than screen %ux%u",cam->width, cam->height, 
				lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
		goto restore;	
	}
	
	/* 重新申请 RGB 缓冲 */
	size = (size_t)cam->width * (size_t)cam->height * 3;
	newbuf = malloc(size);
	if (newbuf == NULL) {
		LOG_ERROR("malloc rgb buffer (%lu bytes): %s",
		          (unsigned long)size, strerror(errno));
		goto restore;
	}
	memset(newbuf, 0, size);
	
	camera_set_fps_if_needed(cam, 1, 30);
	
	/* camera_start */
	if(camera_start(cam, cfg->buffers) < 0){
		goto restore;
	}	

	pfd.fd = cam->fd;
	pfd.events = POLLIN;
	if(poll(&pfd , 1 , 5000) <= 0 || !(pfd.revents & POLLIN)){
		LOG_ERROR("res_switch: poll error");
		goto stop;
	}

	/* 更新 offset、rgbbuf */	
	lb_p->x_offset = ((int)lcd_var_screeninfo.xres - (int)cam->width) / 2;
	lb_p->y_offset = ((int)lcd_var_screeninfo.yres - (int)cam->height) / 2;
	
	free(*rgbbuf_p);
	*rgbbuf_p = newbuf;
	newbuf = NULL;
	
	lcd_clear();
	return 0;
	
stop:
	camera_stop(cam);
	
restore:
	free(newbuf);
	if(camera_set_format(cam, (int)restore_width,(int)restore_height, cam->pixfmt) < 0 ){
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
		LOG_ERROR("res_switch: failed to restore preview");

	return ret;
}

/*
 * 返回值：
 *	 0	拍照成功，预览已恢复
 *	 1	拍照失败，但预览已恢复（可以继续跑）
 *	-1	预览恢复失败（必须退出）
 */
int camera_grab_jpeg(camera_t *cam,const app_config_t *cfg,
		unsigned char **outdata,size_t *out_len)
{
	struct v4l2_buffer buf;
	struct pollfd pfd;
	size_t n;
	unsigned char *data = NULL;
	int ret = 1;

	*outdata = NULL;
	*out_len = 0;
	
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

	data = malloc(n);
	if (data == NULL) {
		LOG_ERROR("snapshot: malloc %lu: %s", (unsigned long)n, strerror(errno));
		camera_queue(cam, &buf);
		goto stop;
	}

	memcpy(data, cam->mem[buf.index], n);
	camera_queue(cam, &buf);

	*outdata = data;
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
 * 返回值：
 *	 0	拍照成功，预览已恢复
 *	 1	拍照失败，但预览已恢复（可以继续跑）
 *	-1	预览恢复失败（必须退出）
 */
static int snapshot_mjpeg(camera_t *cam,const app_config_t *cfg,const char *path,size_t *out_len)
{
	unsigned char *buf = NULL;
	size_t len = 0;
	int ret = camera_grab_jpeg(cam, cfg, &buf, &len);

	if (ret == 0) {
		if (snapshot_save(path, buf, len) < 0)
			ret = 1;
		else
			*out_len = len;
	}

	free(buf);
	return ret;
}

/*
 * 修改：按名字找 /dev/input/eventX。
 * 不写死 event1/event0，避免加了按键驱动之后编号变化。
 * 返回 event 号（0、1、2...），找不到返回 -1。
 */
static int input_find_event(const char *name)
{
	DIR *d = opendir("/sys/class/input");
	struct dirent *de;
	char path[128];
	char buf[128];
	int fd, n;

	if (d == NULL)
		return -1;

	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;

		snprintf(path, sizeof(path),
		         "/sys/class/input/%s/device/name", de->d_name);

		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;

		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n <= 0)
			continue;

		buf[n] = '\0';
		buf[strcspn(buf, "\n")] = '\0';

		if (strcmp(buf, name) == 0) {
			int ev = atoi(de->d_name + 5);
			closedir(d);
			return ev;
		}
	}

	closedir(d);
	return -1;
}

/*
 * 修改：处理按键事件。
 * 只处理 EV_KEY 的按下（value=1），抬起忽略。
 */
static void handle_buttons(int fd)
{
	struct input_event ev;

	while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (ev.type != EV_KEY || ev.value != 1)
			continue;

		if (ev.code == KEY_CAMERA || ev.code == KEY_F1 || ev.code == KEY_1)
			g_save = 1;
		else if (ev.code == KEY_F2 || ev.code == KEY_2)
			g_res = 1;
	}
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
	int tch_ok = 0;
	int http_ok = 0;
	int bl_ok = 0;		/* 修改：背光是否打开成功 */
	int http_fd = -1;
	int poll_timeout_cnt = 0;

	struct fb_var_screeninfo lcd_var_screeninfo;
	struct sigaction sa;
	struct timespec ts;
	struct tm tm;

	camera_t cam;
	backlight_t bl;		/* 修改：背光句柄 */
	letterbox_t lb;
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
	double fps = 0.0;
	
	char tbuf[32];
	char filename[512];
	const char *ext = "jpg";	

	memset(&cam, 0, sizeof(cam));
	cam.fd = -1;
	/* 修改：背光先置成"没打开"，cleanup 里只看 bl_ok */
	memset(&bl, 0, sizeof(bl));
	bl.fd = -1;
	memset(&camera_cfg, 0, sizeof(camera_cfg));
	memset(&lcd_cfg, 0, sizeof(lcd_cfg));
	memset(&lb, 0, sizeof(lb));
	

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

	/*
	 * 修改：3.5 背光。
	 * 失败只告警不退出——采集和显示才是主功能，背光没有也能跑。
	 * 放在日志重定向之后，这一行才会写进 camera.log。
	 */
	if (backlight_init(&bl, NULL, BACKLIGHT_DEFAULT_PERCENT) == 0)
		bl_ok = 1;
	else
		LOG_WARN("backlight init failed, continue without it");

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
	lb.x_offset = ((int)lcd_var_screeninfo.xres - (int)cam.width) / 2;
	lb.y_offset = ((int)lcd_var_screeninfo.yres - (int)cam.height) / 2;
	
	if (lb.x_offset < 0 || lb.y_offset < 0) {
		LOG_ERROR("camera %ux%u larger than screen %ux%u",
		          cam.width, cam.height,
		          lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
		exit_code = -1;
		goto cleanup;
	}

	/* 5. 触摸屏 */
	touch_t tch;
	int touch_ev;
	char touch_dev[32];

	/* 修改：按名字找 goodix-ts，找不到再退回 event1 */
	touch_ev = input_find_event("goodix-ts");
	if (touch_ev >= 0)
		snprintf(touch_dev, sizeof(touch_dev), "/dev/input/event%d", touch_ev);
	else
		snprintf(touch_dev, sizeof(touch_dev), "/dev/input/event1");

	if(touch_open(&tch, touch_dev) < 0){
		LOG_WARN("touch open %s failed", touch_dev);
	}
	else{
		tch_ok = 1;
		/*
		 * 驱动通过 EVIOCGABS 报的是 800x480，但它实际发的是 1024x600 的屏幕坐标
		 * （log 里出现过 x=1267，说明原始值早就超过 800）。
		 * 用 evtest 摸四角确认实际范围后，在这里覆盖掉。
		 */
		touch_set_range(&tch, 0, 1023, 0, 599);
	}
	
	/* 6. HTTP 服务 */
	http_fd = http_init();
	if (http_fd < 0) {
		LOG_WARN("http init failed, continue without http");
	} else {
		http_ok = 1;
	}
	
	/* 7. 信号 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sigusr1;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		LOG_ERROR("sigaction SIGUSR1: %s", strerror(errno));
		exit_code = -1;
		goto cleanup;
	}
	sa.sa_handler = on_sigusr2;
	if (sigaction(SIGUSR2, &sa, NULL) < 0) {
		LOG_ERROR("sigaction SIGUSR2: %s", strerror(errno));
		exit_code = -1;
		goto cleanup;
	}
	sa.sa_handler = on_sigint;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		LOG_ERROR("sigaction SIGINT: %s", strerror(errno));
		exit_code = -1;
		goto cleanup;
	}

	/* 8. RGB 缓冲 */
	size = (size_t)cam.width * (size_t)cam.height * 3;
	rgbbuf = malloc(size);
	if (rgbbuf == NULL) {
		LOG_ERROR("malloc rgb buffer (%lu bytes): %s",
		          (unsigned long)size, strerror(errno));
		exit_code = -1;
		goto cleanup;
	}
	memset(rgbbuf, 0, size);

	/* 9. 起流 */
	if (camera_start(&cam, camera_cfg.buffers) < 0) {
		exit_code = -1;
		goto cleanup;
	}
	
	/*
	 * poll 四个来源：摄像头、触摸、HTTP 监听、按键。
	 * 后三个都是可选的，所以用 idx 记录它们在数组里的位置。
	 */
	struct pollfd fds[4];
	int nfds = 0;
	int touch_idx = -1;
	int http_idx = -1;
	int key_idx = -1;
	int key_fd = -1;
	int key_ev;
	char key_dev[32];

	fds[nfds].fd = cam.fd;      fds[nfds].events = POLLIN;  nfds++;
	if (tch_ok) {
		touch_idx = nfds;
		fds[nfds].fd = tch.fd;  fds[nfds].events = POLLIN;  nfds++;
	}
	if (http_ok) {
		http_idx = nfds;
		fds[nfds].fd = http_fd; fds[nfds].events = POLLIN;  nfds++;
	}

	/* 修改：按名字打开自己的 lc-button 驱动注册的 input 设备 */
	key_ev = input_find_event("lc-button");
	if (key_ev >= 0) {
		snprintf(key_dev, sizeof(key_dev), "/dev/input/event%d", key_ev);
		key_fd = open(key_dev, O_RDONLY | O_NONBLOCK);
		if (key_fd >= 0) {
			key_idx = nfds;
			fds[nfds].fd = key_fd;
			fds[nfds].events = POLLIN;
			nfds++;
			LOG_INFO("button: %s", key_dev);
		} else {
			LOG_WARN("open %s failed: %s", key_dev, strerror(errno));
		}
	} else {
		LOG_WARN("lc-button not found");
	}

	last_t0 = get_time_ms();
	int timeout_ms = (frames == 0)? 5000 : 2000; 
	while (!g_stop) {
		fds[0].revents = 0;
		t0 = get_time_ms();

		ret = poll(fds, nfds, timeout_ms);
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
		
		/* 读触摸屏事件 */
		if (touch_idx >= 0 && (fds[touch_idx].revents & POLLIN)) {
			int tx, ty;
			int r = touch_read(&tch, &tx, &ty);
			if (r < 0) {
				LOG_WARN("touch read error");
			} else if (r == 1) {
				/*
				 * 修改：先看手势。
				 * 上下滑只动亮度，不参与左右分区，所以不会误触发拍照。
				 */
				if (tch.gesture == TOUCH_GESTURE_SWIPE_UP) {
					LOG_INFO("swipe up");
					g_bright = 1;
				} else if (tch.gesture == TOUCH_GESTURE_SWIPE_DOWN) {
					LOG_INFO("swipe down");
					g_bright = -1;
				} else {
					LOG_INFO("tap: %d,%d", tx, ty);
					if (tx > (int)lcd_var_screeninfo.xres / 2)
						g_res = 1;
					else
						g_save = 1;
				}
			}
		}		

		/* 修改：按键按下 -> 拍照/切分辨率 */
		if (key_idx >= 0 && (fds[key_idx].revents & POLLIN))
			handle_buttons(key_fd);

		/*
		 * 修改：HTTP 只留一个入口。
		 * http_accept 拿到连接 fd 后交给 http_serve，
		 * 由它解析路径并分发到 /snapshot.jpg 或 /stream。
		 * 拍照/推流都会 stop/start 流，所以 sequence 要重置。
		 */
		if (http_idx >= 0 && (fds[http_idx].revents & POLLIN)) {
			int cfd = http_accept(http_fd);

			if (cfd >= 0)
				http_serve(cfd, &cam, &camera_cfg, &g_stop);
			
			/** 推流结束后把触摸队列清空，避免“补触发” **/
			touch_flush(&tch);

			have_sequence = 0;
			poll_timeout_cnt = 0;
			continue;
		}

		/* 检查帧 */
		if (!(fds[0].revents & POLLIN)) {
			if (fds[0].revents & POLLERR || fds[0].revents & POLLHUP) {
				LOG_ERROR("poll exit");
				exit_code = -3;
				break;
			}
			//LOG_WARN("poll revents mismatch: %hd", fds[0].revents);
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
				if (lcd_write_line_rgb888(lb.y_offset + i, lb.x_offset,
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
			fps = n * 1000.0 / dt;
			LOG_INFO("fps: %.1f, wait: %.1f, conv: %.1f, draw: %.1f, dropped: %u, rss: %ld kB",
			         fps, wait_time / n, conv_time / n, draw_time / n,
			         dropped, get_vmrss_kb());
			last_t0 = t3;
			wait_time = conv_time = draw_time = n = 0;
		}
		
		/*
		 * 修改：滑动调亮度。
		 * 一次滑动走一档，写 sysfs 只要几微秒，不会拖慢这一帧。
		 */
		if (g_bright != 0) {
			int delta = (int)g_bright;

			g_bright = 0;
			if (bl_ok && backlight_step(&bl, delta) == 0)
				LOG_INFO("bright: %d%%", backlight_percent(&bl));
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
		
		/* 切换分辨率 */
		if(g_res){
			int r;
			static int sw_index = 2;
			if(sw_index > 3) sw_index = 1;
			res_switch_t res_sw[3] = {{800,600},{640,480},{352,288}};
			double t_switch_s = get_time_ms();
			r = res_switch(&cam, &camera_cfg, &res_sw[sw_index - 1], &rgbbuf,&lb);

			have_sequence = 0;
		    poll_timeout_cnt = 0;
			
			 if (r < 0) {
		        exit_code = -10;
		        g_res = 0;
		        break;
		    }
			if(r == 0){
				LOG_INFO("res switch to %u*%u ( %.0f ms)",
		                 res_sw[sw_index -1].width, res_sw[sw_index - 1].height, get_time_ms() - t_switch_s);
				sw_index++;
			}
			 else
		        LOG_WARN("res switch failed, preview restored");
			 
			 g_res = 0;
		}

		/* osd 绘制 */
		#if 1	//有阴影
		char line[64];
			
		//osd_show_rect(lb.x_offset + 4, lb.y_offset + 4, 200, 40, 0x00000000); //底色
		snprintf(line, sizeof(line), "res: %u*%u", cam.width, cam.height);
		
		osd_show_text_shadow(lb.x_offset + 8, lb.y_offset + 8,
		                     (const unsigned char *)line, 0x00FFFFFF, 0x00000000);

		/* 修改：第二行带上当前亮度，滑一下下一帧就能看到变化 */
		if (bl_ok)
			snprintf(line, sizeof(line), "fps: %.1f  drop: %u  bl: %d%%",
			         fps, dropped, backlight_percent(&bl));
		else
			snprintf(line, sizeof(line), "fps: %.1f  drop: %u", fps, dropped);
		osd_show_text_shadow(lb.x_offset + 8, lb.y_offset + 8 + 18,
		                     (const unsigned char *)line, 0x00FFFFFF, 0x00000000);		

		/* 修改：把滑动的提示补上 */
		snprintf(line, sizeof(line), "tap L: shot / tap R: res / swipe: bright");
		osd_show_text_shadow(lb.x_offset + 8, lb.y_offset + 8 + 36,
		                     (const unsigned char *)line, 0x00FFFFFF, 0x00000000);		
		#else	//无阴影
		unsigned char osd_text[64];
		snprintf((char*)osd_text,sizeof(osd_text), "res: %u*%u, fps: %.1f",cam.width,cam.height, fps);
		
		if(osd_show_text(lb.x_offset + 8, lb.y_offset + 8 ,
			(const unsigned char *)osd_text, 0xffffff) < 0)
			LOG_ERROR("osd_show_text failed");
		#endif
		
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
	if (http_ok)
		http_close(http_fd);
	if (key_fd >= 0)
		close(key_fd);
	free(rgbbuf);

	/* 修改：背光要在 lcd_deinit 之前收，因为 lcd_deinit 会把日志关掉 */
	if (bl_ok)
		backlight_deinit(&bl);

	if (lcd_ok)
		lcd_deinit();
	if(tch_ok)
		touch_close(&tch);

	return (exit_code != 0) ? -1 : 0;
}
