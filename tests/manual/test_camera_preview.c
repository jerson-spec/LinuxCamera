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
#include "log.h"

static int camera_fd = -1;
static volatile sig_atomic_t g_stop = 0;


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

static double get_time_ms(void)
{
	struct timespec ts;

	// 获取单调时钟时间
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		LOG_ERROR("clock_gettime");
		return 0.0;
	}

	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0 ;
}

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;         
}

static void Wrapping_up(unsigned char* buf)
{
	if(buf != NULL)
		free(buf);
	if(camera_fd >= 0)
		close(camera_fd);

	if(lcd_deinit() < 0)
	{
		fprintf(stderr,"%s,lcd deinit error\n",__FUNCTION__);
	}
}

static unsigned int pixfmt_from_string(const char *s)
{
    if (!strcasecmp(s, "MJPG") || !strcasecmp(s, "MJPEG"))
        return V4L2_PIX_FMT_MJPEG;
    if (!strcasecmp(s, "YUYV"))
        return V4L2_PIX_FMT_YUYV;
    return 0;
}


/*
*	./test_camera_preview [path/camera.ini] [path/lcd.ini]
*/
int main(int argc,char**argv)
{
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

	if (config_load(camera_cfg_path, &camera_cfg) != 0)
	{
		fprintf(stderr,"%s\n","can't get camera cfg.value"); 
		return -1;
	}
	  
	unsigned int pixfmt = pixfmt_from_string(camera_cfg.format);
	if (pixfmt == 0) {
		fprintf(stderr, "unsupported format: %s\n", camera_cfg.format);
		return -1;
	}

	if (mkdir(camera_cfg.save_dir, 0755) < 0 && errno != EEXIST) {
		perror("mkdir");
		return -1;
	}
	config_print(&camera_cfg);

	
	/** lcd **/
	char *lcd_cfg_path = (argc > 2)? argv[2]: "config/lcd.ini";
	app_config_t lcd_cfg;
	memset(&lcd_cfg, 0, sizeof(camera_cfg));

	if (config_load(lcd_cfg_path, &lcd_cfg) != 0)
	{
		fprintf(stderr,"%s\n","can't get lcd cfg.value"); 
		return -1;
	}
	config_print(&lcd_cfg);
	

	/* 解析日志并初始化 */
	log_level_t log_level;
	if(log_level_parse(camera_cfg.log_level,&log_level) < 0)
	{
		fprintf(stderr,"parse log level %s error\n",camera_cfg.log_level);
		return -1;
	}

	if(log_init(log_level,camera_cfg.log_file) < 0)
	{
		fprintf(stderr,"log %s init error %s\n",camera_cfg.log_file,strerror(errno));
	}

	/* lcd初始化 */
	if(lcd_init(lcd_cfg_path , &lcd_cfg) < 0)
	{
		fprintf(stderr,"%s,lcd init error\n",__FUNCTION__);
		return -1;
	}
	
	Is_full_range(&lcd_cfg);
	
	int ret;
	int i = 0;
	//int j = 0;
	int buf_length[camera_cfg.buffers];
	void *mem[camera_cfg.buffers];
	unsigned char* rgbbuf;
	int h,w;
	size_t size;
	struct fb_fix_screeninfo lcd_fix_screeninfo;
	struct fb_var_screeninfo lcd_var_screeninfo;
	unsigned int dq,q,frames;
	double t0,t1,t2,t3,last_t0;
	double wait_time,conv_time,draw_time,n;
	last_t0 = wait_time = conv_time = draw_time = n = 0.0;
	dq = q = frames = 0;

	Get_lcd_parameters(&lcd_fix_screeninfo,&lcd_var_screeninfo);
	
	/* 打开设备 */
	/** 相机 **/
	camera_fd = open(camera_cfg.device , O_RDWR);
	if(camera_fd < 0)
	{
		fprintf(stderr,"open %s error\n", camera_cfg.device);
		Wrapping_up(NULL);
		return -1;
	}

	/* 获取设备属性 */
	struct v4l2_capability cap;
	ret = ioctl(camera_fd, VIDIOC_QUERYCAP, &cap);
	
    if(ret < 0) {
        fprintf(stderr,"unable to query device %s.\n", camera_cfg.device);
		Wrapping_up(NULL);
        return -1;
    }

    if((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        fprintf(stderr,"%s\n","video capture not supported.");
		Wrapping_up(NULL);
        return -1;
    }

  	if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
	    fprintf(stderr,"%s does not support streaming i/o\n", camera_cfg.device);
		Wrapping_up(NULL);
		return -1; 
	}
	
	/* 设置视频采集参数 */ 
	struct v4l2_format vfmt;
	memset(&vfmt, 0, sizeof(struct v4l2_format));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = (__u32)camera_cfg.width;
	vfmt.fmt.pix.height =(__u32)camera_cfg.height;
	vfmt.fmt.pix.pixelformat = pixfmt;
	
	ret = ioctl(camera_fd, VIDIOC_S_FMT, &vfmt);
	if(ret < 0)
	{
	   fprintf(stderr,"Unable to set format: %d res: %dx%d\n",
	   	pixfmt, vfmt.fmt.pix.width, vfmt.fmt.pix.height);
	   
	   Wrapping_up(NULL);
	   return -1;
 	}

	/** 请求30fps **/
	struct v4l2_streamparm parm;
	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(camera_fd, VIDIOC_G_PARM, &parm) == 0 &&
	(parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
	parm.parm.capture.timeperframe.numerator= 1;
	parm.parm.capture.timeperframe.denominator = 30;
	if (ioctl(camera_fd, VIDIOC_S_PARM, &parm) < 0)
	LOG_WARN("S_PARM 30fps failed: %s", strerror(errno));
	}
	else if (ioctl(camera_fd, VIDIOC_S_PARM, &parm) == 0)
    LOG_INFO("capture fps: %u/%u",
             parm.parm.capture.timeperframe.numerator,
             parm.parm.capture.timeperframe.denominator);

	/** 检查stride **/
	if(vfmt.fmt.pix.bytesperline == vfmt.fmt.pix.width * 2)
	{
		printf("bytesperline: %u , width * 2: %u\n",vfmt.fmt.pix.bytesperline , vfmt.fmt.pix.width * 2);
	}
	else
	{
		perror("bytesperline");
		Wrapping_up(NULL);
	   	return -1;
	}
	/** 检查分辨率匹配：期望设置值、相机支持值、lcd分辨率 **/
	if (vfmt.fmt.pix.width != (unsigned int)camera_cfg.width ||
    vfmt.fmt.pix.height != (unsigned int)camera_cfg.height) {
    fprintf(stderr,
            "resolution adjusted: requested %dx%d, actual %ux%u\n",
            camera_cfg.width, camera_cfg.height,
            vfmt.fmt.pix.width, vfmt.fmt.pix.height);
	}

	if (vfmt.fmt.pix.width != lcd_var_screeninfo.xres ||
    vfmt.fmt.pix.height != lcd_var_screeninfo.yres) {
    fprintf(stderr,
            "camera %ux%u != lcd %ux%u, image will not fill screen\n",
            vfmt.fmt.pix.width, vfmt.fmt.pix.height,
            lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
	}

	/* 申请帧缓冲 */
	struct v4l2_requestbuffers reqbuffer;
    memset(&reqbuffer, 0, sizeof(struct v4l2_requestbuffers));
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.count = camera_cfg.buffers;
	reqbuffer.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(camera_fd, VIDIOC_REQBUFS, &reqbuffer);	
	if(ret < 0) {
       fprintf(stderr,"%s\n","Unable to allocate buffers");
	   
		Wrapping_up(NULL);
		return -1;
    }
	
	/* 映射buffer */
	 for(i = 0; i < camera_cfg.buffers; i++) {
		struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(camera_fd, VIDIOC_QUERYBUF, &buf);
        if(ret < 0) {
            fprintf(stderr,"%s\n","Unable to query buffer");
			
			Wrapping_up(NULL);
			return -1;
        }
		buf_length[i] = buf.length;
        mem[i] = mmap(0 /* start anywhere */ ,
                          buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera_fd,
                          buf.m.offset);
        if(mem[i] == MAP_FAILED) {
            fprintf(stderr,"%s\n","Unable to map buffer");
			
			Wrapping_up(NULL);
			return -1;
        }
    }
	 
	/* 申请到的帧缓冲放入队列 */
	for(i = 0; i < camera_cfg.buffers; ++i) {		
		struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(camera_fd, VIDIOC_QBUF, &buf);
        if(ret < 0) {
            fprintf(stderr,"line: %d,Unable to queue buffer",__LINE__);

			for (i = 0; i < camera_cfg.buffers; i++) {
	        	munmap(mem[i], buf_length[i]);
			}
			Wrapping_up(NULL);
			
			return -1;
        }
    }
	/* 开始图片采集 */
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(camera_fd, VIDIOC_STREAMON, &type);
	if(ret < 0) {
	   // printf("Unable to start capture");
		LOG_ERROR("Unable to start capture");
	   for (i = 0; i < camera_cfg.buffers; i++) {
	        	munmap(mem[i], buf_length[i]);
		}
		Wrapping_up(NULL);
		return -1;
	}

	/** 注册信号处理函数 **/
	if (signal(SIGINT, on_sigint) == SIG_ERR) {
    	perror("signal");
		for (i = 0; i < camera_cfg.buffers; i++) {
	        	munmap(mem[i], buf_length[i]);
		}
		Wrapping_up(NULL);
		return -1;
	}

	/** 分配rgbbuf空间并置初值0 **/
	w = vfmt.fmt.pix.width;
	h = vfmt.fmt.pix.height;
	
	size = (size_t)w * (size_t)h * 3;
	rgbbuf = malloc(size);
	if(rgbbuf == NULL)
	{
		fprintf(stderr, "%s,rgbbuf malloc error\n",__FUNCTION__);

		for (i = 0; i < camera_cfg.buffers; i++) {
       	 	munmap(mem[i], buf_length[i]);
		}
		Wrapping_up(NULL);
		return -1;
	}
	memset(rgbbuf , 0 , size);
		
	/* 监听有数据可读 */
	struct pollfd fds[1];
	fds[0].fd = camera_fd;
	fds[0].events = POLLIN;

	unsigned int last_sequence,have_sequence,dropped;
	last_sequence = have_sequence = dropped = 0;
	int poll_timeout_cnt = 0;
	int exit_code = 0;
	
	last_t0 = get_time_ms();
	while(!g_stop)
	{
		fds[0].revents = 0;
		t0 = get_time_ms();
		
		ret = poll(&fds[0], 1, 2000);
		if(ret == 0) 
		{
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
		else if(ret < 0)
		{
			if(errno == EINTR) 
			{
				ret = 0;
				continue;
			}
			LOG_ERROR("poll %s",strerror(errno));
			exit_code = -2;
			break;
		}
		else 
		{
			if(!(fds[0].revents & POLLIN))
			{	
				if(fds[0].revents & POLLERR || fds[0].revents & POLLHUP)
				{
					LOG_ERROR("poll exit");
					exit_code = -3;
					break;
				}
				LOG_WARN("poll revents mismatch: %hd",fds[0].revents);
				continue;
			}
						
			/* 获得采集数据 */
		 	struct v4l2_buffer buf;
		    memset(&buf, 0, sizeof(struct v4l2_buffer));
		    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		    buf.memory = V4L2_MEMORY_MMAP;

		    ret = ioctl(camera_fd, VIDIOC_DQBUF, &buf);
		    if(ret < 0) {
				LOG_ERROR("dequeue buffer: %s",strerror(errno));
				exit_code = -4;
				break;
		    }	
			dq++;
			poll_timeout_cnt = 0;
			
			t1 = get_time_ms();

			/** 统计丢帧（序列号） **/
			if(have_sequence && (buf.sequence - last_sequence) > 1)
			{
				dropped += buf.sequence - last_sequence - 1;
			}
			have_sequence = 1;
			last_sequence = buf.sequence;
			
			/** 检查buf.bytesused **/
			if(buf.bytesused < (unsigned int)(w * h * 2))
			{
				fprintf(stderr, "short YUYV frame: %u < %d\n",buf.bytesused, w * h * 2);
				
				/*** buffer放回队列 ***/
				ret = ioctl(camera_fd, VIDIOC_QBUF, &buf);
		        if(ret < 0) {
		            LOG_ERROR("line: %d,Unable to queue buffer",__LINE__);
					exit_code = -5;
					break;
		        }
				q++;
				continue;
			}
			
			/* yuyv_to_rgb */
			ret = yuyv_to_rgb(mem[buf.index],rgbbuf,vfmt.fmt.pix.width,vfmt.fmt.pix.height);
			if(ret < 0)
			{
				LOG_ERROR("yuyv to rgb: %s",strerror(errno));
				exit_code = -6;
				break;
			}
			t2 = get_time_ms();

			/* buffer放回队列 */
			ret = ioctl(camera_fd, VIDIOC_QBUF, &buf);
	        if(ret < 0) {
	            LOG_ERROR("line: %d,Unable to queue buffer",__LINE__);
				exit_code = -7;
				break;
	        }
			frames++;
			q++;
			
			/* lcd_write_line_rgb888 */
			unsigned int w_offset = (lcd_var_screeninfo.xres - (unsigned int)w) / 2;
			unsigned int h_offset = (lcd_var_screeninfo.yres - (unsigned int)h) / 2;
			
			for(i = 0 ; i < h ; i++ )
			{
				ret = lcd_write_line_rgb888(i + h_offset, w_offset, rgbbuf + (size_t)i * 3 * (size_t)w, w);
				if(ret < 0)
				{
					LOG_ERROR("lcd write line error, line: %d,",i);
					break;
				}
				
			}
			if(ret < 0)
			{
				exit_code = -9;
				break;
			}
			
			t3 = get_time_ms();
			wait_time += t1 - t0;
			conv_time += t2 - t1;
			draw_time += t3 - t2;
			n++;
			
			/** 每一秒打印一次信息 **/ 
			if((t3 - last_t0) >= 1000.0)
			{
				double dt = t3 - last_t0;
				
				//这一秒内的帧数、这一秒内每一帧的wait,conv,draw时间、全时间段内总丢帧数
				LOG_INFO("fps: %.1f, wait: %.1f, conv: %.1f, draw: %.1f, dropped: %u, rss: %ld kB",
					n * 1000.0 / dt , wait_time / n , conv_time / n , draw_time / n , dropped, get_vmrss_kb());
				last_t0 = t3;
				wait_time = conv_time = draw_time = n = 0;
			}

			/** double buffering choice **/
			if(lcd_doublebuffering_on())
			{
				if(lcd_flip() < 0)
				{
					LOG_ERROR("line: %d , lcd filp error",__LINE__);
					LOG_INFO("double buffering downgrade --> single buffering");
					continue;
				}
			}
		}
	}

	LOG_INFO("DQbuf: %u, Qbuf: %u, Frames:%u",dq,q,frames);

	/* 停止图片的采集 */
	 if(ioctl(camera_fd, VIDIOC_STREAMOFF, &type) != 0) {
        perror("stop capture");
 	}
	
	/* 释放buffer、收尾 */
	for (i = 0; i < camera_cfg.buffers; i++) {
	      munmap(mem[i], buf_length[i]);
	}
	
	Wrapping_up(rgbbuf);
	
	if(exit_code != 0)
	{
		LOG_ERROR("error exit_code: %d",exit_code);
		return -1;
	}
	
	return 0;
}
