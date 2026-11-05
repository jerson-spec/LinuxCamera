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

#include "lcd.h"
#include "yuyv.h"
#include "log.h"

static int camera_fd;

static void Wrapping_up(unsigned char* buf)
{
	if(buf != NULL)
		free(buf);
	if(camera_fd != 0)
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
*	./test_Get_images [path/camera.ini] [path/lcd.ini]
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
	int j = 0;
	int buf_length[camera_cfg.buffers];
	void *mem[camera_cfg.buffers];
	unsigned char* rgbbuf;
	size_t index;
	unsigned char r,g,b;
	unsigned int color;
	int h,w;
	int time;
	size_t size;
	struct fb_fix_screeninfo lcd_fix_screeninfo;
	struct fb_var_screeninfo lcd_var_screeninfo;

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
		return ret;
	}

	/* 监听有数据可读 */
	struct pollfd fds[1];
	fds[0].fd = camera_fd;
	fds[0].events = POLLIN;
	if(poll(&fds[0], 1, 20000) == 1)
	{
		/* 获得采集数据 */
	 	struct v4l2_buffer buf;
	    memset(&buf, 0, sizeof(struct v4l2_buffer));
	    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory = V4L2_MEMORY_MMAP;

	    ret = ioctl(camera_fd, VIDIOC_DQBUF, &buf);
	    if(ret < 0) {
			perror("dequeue buffer");

			for (i = 0; i < camera_cfg.buffers; i++) {
		        munmap(mem[i], buf_length[i]);
			}
			Wrapping_up(NULL);
			return -1;
	    }		
		
		/* yuyv_to_rgb */
		w = vfmt.fmt.pix.width;
		h = vfmt.fmt.pix.height;

		/** 检查buf.bytesused **/
		if(buf.bytesused < (unsigned int)(w * h * 2))
		{
			fprintf(stderr, "short YUYV frame: %u < %d\n",buf.bytesused, w * h * 2);
			
			for (i = 0; i < camera_cfg.buffers; i++) {
		        munmap(mem[i], buf_length[i]);
			}
			Wrapping_up(NULL);
			return -1;
		}
		
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
		
		ret = yuyv_to_rgb(mem[buf.index],rgbbuf,vfmt.fmt.pix.width,vfmt.fmt.pix.height);
		if(ret < 0)
		{
			perror("yuyv to rgb");
			for (i = 0; i < camera_cfg.buffers; i++) {
	        	munmap(mem[i], buf_length[i]);
			}
			Wrapping_up(rgbbuf);
			return -1;
		}
		
		/* lcd_put_pixe */
		time = lcd_cfg.lcd_display_time;
		while(time--)
		{
			for(i = 0 ; i < h ; i++ )
			{
				for(j = 0 ; j < w ; j++)
				{
					index = (size_t)i * (size_t)w + (size_t)j;

					r = rgbbuf[3 * index + 0];
					g = rgbbuf[3 * index + 1];
					b = rgbbuf[3 * index + 2];
					color = ((unsigned int)r << 16) | ((unsigned int)g << 8 ) | ((unsigned int)b) ;
					 
					ret = lcd_put_pixel((unsigned int)j , (unsigned int)i , color);
					if(ret < 0)
					{
						fprintf(stderr, "lcd put pixel error, line:%d,column:%d\n",j,i);
						for (i = 0; i < camera_cfg.buffers; i++) {
				        	munmap(mem[i], buf_length[i]);
						}
						Wrapping_up(rgbbuf);
						return -1;
					}
				}
			}	
			sleep(1);
		}
		
		/* buffer放回队列 */
		ret = ioctl(camera_fd, VIDIOC_QBUF, &buf);
        if(ret < 0) {
            fprintf(stderr,"line: %d,Unable to queue buffer",__LINE__);
			for (i = 0; i < camera_cfg.buffers; i++) {
	        	munmap(mem[i], buf_length[i]);
			}
			Wrapping_up(rgbbuf);
			return -1;
        }
	}
	else 
	{
		perror("poll");
		for (i = 0; i < camera_cfg.buffers; i++) {
        	munmap(mem[i], buf_length[i]);
		}
		Wrapping_up(NULL);
		return -1;
	}

	/* 停止图片的采集 */
	 if(ioctl(camera_fd, VIDIOC_STREAMOFF, &type) != 0) {
        perror("stop capture");
		for (i = 0; i < camera_cfg.buffers; i++) {
        	munmap(mem[i], buf_length[i]);
		}
		Wrapping_up(rgbbuf);
        return -1;
 	}
	
	/* 释放buffer、收尾 */
	for (i = 0; i < camera_cfg.buffers; i++) {
	        munmap(mem[i], buf_length[i]);
	}
	
	Wrapping_up(rgbbuf);
	
	return 0;
}
