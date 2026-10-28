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

#include "config.h"
#include "log.h"
#include <strings.h>


//#define buffer_cnt 4

static unsigned int pixfmt_from_string(const char *s)
{
    if (!strcasecmp(s, "MJPG") || !strcasecmp(s, "MJPEG"))
        return V4L2_PIX_FMT_MJPEG;
    if (!strcasecmp(s, "YUYV"))
        return V4L2_PIX_FMT_YUYV;
    return 0;
}

/*
*	./test_Get_images
*/
int main(int argc,char** argv)
{
	if(argc > 2 )
	{
		printf("Usage:%s [ini path/camera.ini]\n",argv[0]);
		return -1;
	}

	/* 解析配置项并打印 */
	const char *cfg_path = (argc > 1)? argv[1]: "config/camera.ini";
    app_config_t cfg;

    if (config_load(cfg_path, &cfg) != 0)
    {
		printf("can't get cfg.value\n"); 
		return -1;
	}
      
    config_print(&cfg);

	unsigned int pixfmt = pixfmt_from_string(cfg.format);
    if (pixfmt == 0) {
        fprintf(stderr, "unsupported format: %s\n", cfg.format);
        return -1;
    }

    if (mkdir(cfg.save_dir, 0755) < 0 && errno != EEXIST) {
        perror("mkdir");
        return -1;
    }

	/* 解析日志并初始化 */
	log_level_t log_level;
	if(log_level_parse(cfg.log_level,&log_level) < 0)
	{
		fprintf(stderr,"parse log level %s error\n",cfg.log_level);
		return -1;
	}

	if(log_init(log_level,cfg.log_file) < 0)
	{
		fprintf(stderr,"log %s init error %s\n",cfg.log_file,strerror(errno));
	}
	
	int fd;
	int file_fd;
	char filename[512];
	int ret;
	int i = 0;
	int j = 0;
	int buf_length[cfg.buffers];
	void *mem[cfg.buffers];
	
	//int default_pix_width;
	//int default_pix_height;

	/* 打开设备 */
	fd = open(cfg.device , O_RDWR);
	if(fd < 0)
	{
		printf("open %s error\n", cfg.device);
		return -1;
	}
	
	/* 获取设备属性 */
	struct v4l2_capability cap;
	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	
    if(ret < 0) {
        printf("unable to query device %s.\n", cfg.device);
        return -1;
    }

    if((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        printf("video capture not supported.\n");
        return -1;
    }

  	if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
	    printf("%s does not support streaming i/o\n", cfg.device);
		return -1; 
	}

	/* 查看当前输入源 */
	int value;
	
	if(ioctl(fd,VIDIOC_G_INPUT,&value) < 0) 
	{
		printf("can't get current input source\n");
//		return -1;
	}
	else if(ioctl(fd,VIDIOC_G_INPUT,&value) == 0)
		printf("current input source: %d\n",value);

#if 0	
	/* 选择输入源 */	/* 这部分代码有问题！ */
	char *s = cfg.device;
	while(*s != 0)
	{
		if(*s >= '0' && *s <= '9')
		{
			value = *s -48;
			break;
		}
		s++;
	}

	printf("value: %d\n",value);
	
	if(ioctl(fd,VIDIOC_S_INPUT,&value) < 0) 
	{
		printf("choose input source %d error\n",value);
		return -1;
	}
#endif

	/* 获得设备支持的所有格式并打印出来 */	
	if(cfg.flag)
	{
		struct v4l2_fmtdesc fmtdesc;
	    memset(&fmtdesc, 0, sizeof(struct v4l2_fmtdesc));
	    fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		while(1)		 //使用while循环从0开始测试index索引值
		{
			fmtdesc.index = i;
			int ret = ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc);
			if (ret < 0)
			{
				printf("all device supported formats listed\n");
				break;
			}
			else
			{
				printf("index=%d\n",fmtdesc.index);
				i++;
			}

			while(1)
			{
				/* 获得设备支持的每一种格式支持的分辨率并打印出来 */
				struct v4l2_frmsizeenum fsenum;
				memset(&fsenum, 0, sizeof(struct v4l2_frmsizeenum));
				fsenum.pixel_format = fmtdesc.pixelformat;
				fsenum.index = j;
				
				if(ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsenum) == 0) {
					if(fsenum.type == V4L2_FRMIVAL_TYPE_DISCRETE)
					{
						printf("format:%s,%d , support frame size: %d x %d\n",\
						fmtdesc.description,fsenum.pixel_format,fsenum.discrete.width,fsenum.discrete.height);
						
					}
					else if (fsenum.type == V4L2_FRMSIZE_TYPE_STEPWISE ||fsenum.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
						printf("format:%s,%d , support frame size:\n",fmtdesc.description,fsenum.pixel_format);
		        		printf("  %ux%u .. %ux%u, step %ux%u\n",fsenum.stepwise.min_width,fsenum.stepwise.min_height,\
		               fsenum.stepwise.max_width,fsenum.stepwise.max_height,fsenum.stepwise.step_width,fsenum.stepwise.step_height);
   					}
					j++;
				}
				else
				{
					j = 0;
					break;
				}
			}
			
		}
	}	

	else
	{
		/* 设置视频采集参数 */ 
		struct v4l2_format vfmt;
		memset(&vfmt, 0, sizeof(struct v4l2_format));
		vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vfmt.fmt.pix.width = cfg.width;
		vfmt.fmt.pix.height = cfg.height;
		vfmt.fmt.pix.pixelformat = pixfmt;
		ret = ioctl(fd, VIDIOC_S_FMT, &vfmt);
		if(ret < 0)
		{
		   printf("Unable to set format: %d res: %dx%d\n",\
		   	pixfmt, vfmt.fmt.pix.width, vfmt.fmt.pix.height);
		   return -1;
	 	}

		/* 申请帧缓冲 */
		struct v4l2_requestbuffers reqbuffer;
	    memset(&reqbuffer, 0, sizeof(struct v4l2_requestbuffers));
		reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		reqbuffer.count = cfg.buffers;
		reqbuffer.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuffer);	
		if(ret < 0) {
	        printf("Unable to allocate buffers");
			return -1;
	    }

		/* 映射buffer */
	    for(i = 0; i < cfg.buffers; i++) {
			struct v4l2_buffer buf;
	        memset(&buf, 0, sizeof(struct v4l2_buffer));
	        buf.index = i;
	        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	        buf.memory = V4L2_MEMORY_MMAP;
	        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
	        if(ret < 0) {
	            printf("Unable to query buffer");
				return -1;
	        }
			buf_length[i] = buf.length;
	        mem[i] = mmap(0 /* start anywhere */ ,
	                          buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	                          buf.m.offset);
	        if(mem[i] == MAP_FAILED) {
	            printf("Unable to map buffer");
				return -1;
	        }
	    }

		/* 申请到的帧缓冲放入队列 */
	    for(i = 0; i < cfg.buffers; ++i) {		
			struct v4l2_buffer buf;
	        memset(&buf, 0, sizeof(struct v4l2_buffer));
	        buf.index = i;
	        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	        buf.memory = V4L2_MEMORY_MMAP;
	        ret = ioctl(fd, VIDIOC_QBUF, &buf);
	        if(ret < 0) {
	            printf("Unable to queue buffer");
				return -1;
	        }
	    }

		/* 开始图片采集 */
		int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    ret = ioctl(fd, VIDIOC_STREAMON, &type);
	    if(ret < 0) {
	       // printf("Unable to start capture");
			LOG_ERROR("Unable to start capture");
	        return ret;
	    }

		while(1)
		{
			if(j >= cfg.save_count)
				break;
			struct pollfd fds[1];
			fds[0].fd = fd;
			fds[0].events = POLLIN;
		
			/* 监听有数据可读 */
			if(poll(&fds[0], 1, -1) == 1)
			{
				/* 获得采集数据 */
			 	struct v4l2_buffer buf;
			    memset(&buf, 0, sizeof(struct v4l2_buffer));
			    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			    buf.memory = V4L2_MEMORY_MMAP;

			    ret = ioctl(fd, VIDIOC_DQBUF, &buf);
			    if(ret < 0) {
			        printf("Unable to dequeue buffer");
					break;
			    }		

				/* 输出图片 */
				sprintf(filename,"%s/image%02d.jpg",cfg.save_dir,j);	
				
				file_fd = open(filename , O_RDWR | O_CREAT | O_TRUNC, 0666);
				if(file_fd < 0)
				{
					printf("can not save images\n");
					break;
				}
				
				if(write(file_fd, mem[buf.index], buf.bytesused) != (ssize_t)buf.bytesused)
				{
					printf("Short write error\n");
					//continue;
					break;
				}
				else
				{
					printf("Taking picture: %d\n",j);
					//LOG_INFO("Taking picture: image%02d.jpg in %s\n",j,cfg.save_dir);
					LOG_DEBUG("Taking picture: image%02d.jpg in %s\n",j,cfg.save_dir);
					j++;
				}
					
				close(file_fd);

				/* buffer放回队列 */
		        ret = ioctl(fd, VIDIOC_QBUF, &buf);
		        if(ret < 0) {
		            printf("Unable to queue buffer");
					break;
		        }
			 }
			
			else 
			{
				printf("timeout or error,try again\n");
				break;
			}
		}
			
		/* 停止图片的采集 */
	    if(ioctl(fd, VIDIOC_STREAMOFF, &type) != 0) {
	        printf("Unable to stop capture");
	        return -1;
	    }

		/* 释放buffer */
		for (i = 0; i < cfg.buffers; i++) {
	        munmap(mem[i], buf_length[i]);
		}
	}	
	
	close(fd);
	log_close();
	
	return 0;
}


