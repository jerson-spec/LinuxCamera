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

#include "lcd.h"
#include "yuyv.h"
#include "camera.h"

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_save = 0;
camera_t cam_t;

/* 返回当前进程的 VmRSS，单位 kB；失败返回 -1 */
static long get_vmrss_kb(void){
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

/* 获取自系统启动以来时间 */
static double get_time_ms(void){
	struct timespec ts;

	// 获取单调时钟时间
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		LOG_ERROR("clock_gettime");
		return 0.0;
	}

	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0 ;
}

static void on_sigint(int sig){
    (void)sig;
    g_stop = 1;         
}

static void on_sigusr1(int sig){
    (void)sig;
    g_save = 1;         
}

static int snapshot_save(char* path, unsigned char* rgbbuf,int w,int h){
	int file_fd;
	file_fd = open(path , O_RDWR | O_CREAT | O_TRUNC, 0666);
	if(file_fd < 0)
	{
		LOG_ERROR("can not open snapshot file: %s",path);
		return -1;
	}
	
	if((size_t)write(file_fd, rgbbuf, (size_t)w *(size_t) h * 3) != (size_t)w *(size_t) h * 3)
	{
		LOG_ERROR("Short write error");
		close(file_fd);
		return -1;
	}
		
	close(file_fd);
	return 0;
}


/*
*	./test_camera_preview [path/camera.ini] [path/lcd.ini]
*/
int main(int argc,char**argv){
	int ret;
	int i = 0;
	
	unsigned char* rgbbuf;
	size_t size;
	struct fb_var_screeninfo lcd_var_screeninfo;
	unsigned int dq,q,frames;
	double t0,t1,t2,t3,last_t0;
	double wait_time,conv_time,draw_time,n;
	last_t0 = wait_time = conv_time = draw_time = n = 0.0;
	dq = q = frames = 0;
	memset(&cam_t , 0 , sizeof(camera_t));
	
	if(argc > 3 )
	{
		fprintf(stderr,"Usage:%s [path/camera.ini] [path/lcd.ini]\n",argv[0]);
		return -1;
	}

	/* 解析配置项并打印 */
	/** 相机 **/
	char *camera_cfg_path = (argc > 1)? argv[1]: "config/camera.ini";
	app_config_t camera_cfg;
	memset(&camera_cfg, 0, sizeof(camera_cfg));
	
	/** lcd **/
	char *lcd_cfg_path = (argc > 2)? argv[2]: "config/lcd.ini";
	app_config_t lcd_cfg;
	memset(&lcd_cfg, 0, sizeof(camera_cfg));


	/* 初始化设备 */
	/** lcd **/
	if(lcd_init(lcd_cfg_path , &lcd_cfg) < 0){
		fprintf(stderr,"%s, line: %d, lcd init error\n",__FUNCTION__,__LINE__);
		return -1;
	}
	Is_full_range(&lcd_cfg);
	Get_lcd_parameters(NULL, &lcd_var_screeninfo);
	
	/** 相机 **/
	if(camera_init(camera_cfg_path , &camera_cfg , &cam_t) < 0){
		fprintf(stderr,"%s, line: %d, camera init error\n",__FUNCTION__,__LINE__);
		close(cam_t.fd);
		return -1;
	}	

	/* 注册信号处理函数 */
	/** SIGUSR1 **/
	struct sigaction sa;
	memset(&sa , 0 , sizeof(sa));
	
    sa.sa_handler = on_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction SIGUSR1");
		camera_deinit();
        return -1;
    }

	/** SIGINT **/
    sa.sa_handler = on_sigint;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
		camera_deinit();
        return -1;
    }	

	/** 分配rgbbuf空间并置初值0 **/
	size = (size_t)cam_t.w * (size_t)cam_t.h * 3;
	rgbbuf = malloc(size);
	if(rgbbuf == NULL){
		fprintf(stderr,"%s\n","rgbbuf malloc error");
		camera_deinit();
		return -1;
	}
	memset(rgbbuf , 0 , size);	

	/** 创建拍照存图目录、文件名后缀处理 **/	
	struct timespec ts;
	struct tm tm;
    char tbuf[32];
	char filename[512];

	//const char *ext = (!strcasecmp(camera_cfg.save_format, "YUYV")) ? "yuv" : "jpg";
	const char *ext = "rgb";
	
	/* 开始图片采集 */
	if(camera_start() < 0){
		camera_deinit();
		return -1;
	}
		
	/* 监听有数据可读 */
	struct pollfd fds[1];
	fds[0].fd = cam_t.fd;
	fds[0].events = POLLIN;

	unsigned int last_sequence,have_sequence,dropped;
	last_sequence = have_sequence = dropped = 0;
	int poll_timeout_cnt = 0;
	int exit_code = 0;
	
	last_t0 = get_time_ms();
	while(!g_stop){
		fds[0].revents = 0;
		t0 = get_time_ms();
		
		ret = poll(&fds[0], 1, 2000);
		if(ret == 0) {
			LOG_WARN("poll timeout");
			poll_timeout_cnt++;
			if(poll_timeout_cnt >= 3)
			{
				LOG_ERROR("poll timeout: %d times!",poll_timeout_cnt);
				exit_code = -1;
				break;
			}
			continue;
		}
		else if(ret < 0){
			if(errno == EINTR) 
			{
				ret = 0;
				continue;
			}
			LOG_ERROR("poll %s",strerror(errno));
			exit_code = -2;
			break;
		}
		else {
			if(!(fds[0].revents & POLLIN)){	
				if(fds[0].revents & POLLERR || fds[0].revents & POLLHUP){
					LOG_ERROR("poll exit");
					exit_code = -3;
					break;
				}
				LOG_WARN("poll revents mismatch: %hd",fds[0].revents);
				continue;
			}
						
			/* 获得采集数据 */
		 	struct v4l2_buffer buf;
		   if(camera_dqueue(&buf) < 0){
				exit_code = -4;
				break;
		   }
			dq++;
			poll_timeout_cnt = 0;
			
			t1 = get_time_ms();

			/** 统计丢帧（序列号） **/
			if(have_sequence && (buf.sequence - last_sequence) > 1){
				dropped += buf.sequence - last_sequence - 1;
			}
			have_sequence = 1;
			last_sequence = buf.sequence;
			
			/** 检查buf.bytesused **/
			if(buf.bytesused < (unsigned int)(cam_t.w * cam_t.h * 2)){
				LOG_ERROR("short YUYV frame: %u < %d",buf.bytesused, cam_t.w * cam_t.h * 2);
				
				/*** buffer放回队列 ***/
				if(camera_queue(&buf) < 0){
					exit_code = -5;
					break;
				}
				q++;
				continue;
			}
			
			/* yuyv_to_rgb */
			ret = yuyv_to_rgb(cam_t.mem[buf.index],rgbbuf,cam_t.w,cam_t.h);
			if(ret < 0){
				LOG_ERROR("yuyv to rgb: %s",strerror(errno));
				exit_code = -6;
				break;
			}
			t2 = get_time_ms();

			/* buffer放回队列 */
			if(camera_queue(&buf) < 0){
					exit_code = -7;
					break;
				}
			frames++;
			q++;
			
			/* lcd_write_line_rgb888 */
			unsigned int w_offset = (lcd_var_screeninfo.xres - (unsigned int)cam_t.w) / 2;
			unsigned int h_offset = (lcd_var_screeninfo.yres - (unsigned int)cam_t.h) / 2;
			
			for(i = 0 ; i < cam_t.h ; i++ ){
				ret = lcd_write_line_rgb888(i + h_offset, w_offset, rgbbuf + (size_t)i * 3 * (size_t)cam_t.w, cam_t.w);
				if(ret < 0){
					LOG_ERROR("lcd write line error, line: %d,",i);
					break;
				}
			}
			if(ret < 0){
				exit_code = -9;
				break;
			}
			
			t3 = get_time_ms();
			wait_time += t1 - t0;
			conv_time += t2 - t1;
			draw_time += t3 - t2;
			n++;
			
			/** 每一秒打印一次信息 **/ 
			if((t3 - last_t0) >= 1000.0){
				double dt = t3 - last_t0;
				
				//这一秒内的帧数、这一秒内每一帧的wait,conv,draw时间、全时间段内总丢帧数
				LOG_INFO("fps: %.1f, wait: %.1f, conv: %.1f, draw: %.1f, dropped: %u, rss: %ld kB",
					n * 1000.0 / dt , wait_time / n , conv_time / n , draw_time / n , dropped, get_vmrss_kb());
				last_t0 = t3;
				wait_time = conv_time = draw_time = n = 0;
			}

			/** 拍照存图 **/
			double t_shot_s;
			double t_shot_e;
			
			if(g_save){
				t_shot_s = get_time_ms();
				clock_gettime(CLOCK_REALTIME, &ts);
			    localtime_r(&ts.tv_sec, &tm);
			    strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", &tm);
				
				sprintf(filename,"%s/IMG_%s.%s",camera_cfg.save_dir,tbuf,ext);	
				if(snapshot_save(filename, rgbbuf, cam_t.w , cam_t.h) < 0)
				{
					LOG_WARN("save snapshot error, continue");
				}
				t_shot_e = get_time_ms();
				LOG_INFO("snapshot: %s (%u bytes, %.0f ms)",filename,(size_t)cam_t.w *(size_t) cam_t.h * 3, t_shot_e - t_shot_s);
				g_save = 0;
			}
			
			/** double buffering choice **/
			if(lcd_doublebuffering_on()){
				if(lcd_flip() < 0){
					LOG_ERROR("line: %d , lcd filp error",__LINE__);
					LOG_INFO("double buffering downgrade --> single buffering");
				}
			}
		}
	}

	LOG_INFO("DQbuf: %u, Qbuf: %u, Frames:%u",dq,q,frames);

	/* 停止图片的采集 */
	camera_stop();
	 
	/* 释放buffer、收尾 */
	free(rgbbuf);
	camera_deinit();
	
	if(exit_code != 0){
		LOG_ERROR("error exit_code: %d",exit_code);
		return -1;
	}
	return 0;
}
