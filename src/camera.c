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
#include <errno.h>
#include <string.h>
#include <strings.h>

#include "camera.h"
#include "lcd.h"

camera_t cam_t;
struct fb_var_screeninfo lcd_var_screeninfo;

static unsigned int pixfmt_from_string(const char *s){
    if (!strcasecmp(s, "MJPG") || !strcasecmp(s, "MJPEG"))
        return V4L2_PIX_FMT_MJPEG;
    if (!strcasecmp(s, "YUYV"))
        return V4L2_PIX_FMT_YUYV;
    return 0;
}

int camera_init(char* camera_cfg_path , app_config_t *camera_cfg_p, camera_t *cam_t_p){
	int ret;
	int i;
	memset(&cam_t , 0 ,sizeof(camera_t));
	cam_t.fd = -1;
	log_level_t log_level;

	/** 获得lcd参数 **/	
	Get_lcd_parameters(NULL ,&lcd_var_screeninfo);
	
	/* 解析配置项并打印 */
	strncpy(cam_t.cfg_path, camera_cfg_path , 256);
	
	if(config_load(cam_t.cfg_path, &cam_t.cfg) < 0 ){
		fprintf(stderr,"can't load the config: %s\n",cam_t.cfg_path);
		return -1;
	}
	
	*cam_t_p = cam_t;
	*camera_cfg_p = cam_t.cfg;
	
	unsigned int pixfmt = pixfmt_from_string(cam_t.cfg.format);
	if (pixfmt == 0) {
		fprintf(stderr, "unsupported format: %s\n", cam_t.cfg.format);
		return -1;
	}

	if (mkdir(cam_t.cfg.save_dir, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr,"mkdir: %s error\n",cam_t.cfg.save_dir);
		return -1;
	}
	
	if(cam_t.cfg.buffers > camera_max_bufs){
		fprintf(stderr,"cfg buffers: %d out of bounds\n",cam_t.cfg.buffers);
		return  -1;
	}
	
	config_print(&cam_t.cfg);

	/* 初始化日志 */
	if(log_level_parse(cam_t.cfg.log_level, &log_level) < 0 ){
		fprintf(stderr,"parse log: %s error\n",cam_t.cfg.log_level);
		return -1;
	}
	
	if(log_init(log_level, cam_t.cfg.log_file) < 0 ){
		fprintf(stderr,"init log: %s error\n",cam_t.cfg.log_file);
	}

	/* 打开设备 */
	cam_t.fd = open(cam_t.cfg.device , O_RDWR);
	if(cam_t.fd < 0){
		LOG_ERROR("open %s error", cam_t.cfg.device);
		return -1;
	}

	/* 获取设备属性 */
	struct v4l2_capability cap;
	ret = ioctl(cam_t.fd, VIDIOC_QUERYCAP, &cap);
	
    if(ret < 0) {
        LOG_ERROR("unable to query device %s", cam_t.cfg.device);
        return -1;
    }

    if((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        LOG_ERROR("video capture not supported");
        return -1;
    }

  	if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
	    LOG_ERROR("%s does not support streaming i/o", cam_t.cfg.device);
		return -1; 
	}
	
	/* 设置视频采集参数 */ 
	struct v4l2_format vfmt;
	memset(&vfmt, 0, sizeof(struct v4l2_format));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = (__u32)cam_t.cfg.width;
	vfmt.fmt.pix.height =(__u32)cam_t.cfg.height;
	vfmt.fmt.pix.pixelformat = pixfmt;
	
	ret = ioctl(cam_t.fd, VIDIOC_S_FMT, &vfmt);
	if(ret < 0){
	   LOG_ERROR("Unable to set format: %d res: %dx%d",
	   	pixfmt, vfmt.fmt.pix.width, vfmt.fmt.pix.height);
	   return -1;
 	}
	cam_t.w = vfmt.fmt.pix.width;
	cam_t.h = vfmt.fmt.pix.height;

	/** 检查stride **/
	if(vfmt.fmt.pix.bytesperline == vfmt.fmt.pix.width * 2){
		LOG_INFO("bytesperline: %u , width * 2: %u",vfmt.fmt.pix.bytesperline, vfmt.fmt.pix.width * 2);
	}
	else{
		LOG_ERROR("bytesperline mismatch");
	   	return -1;
	}
	/** 检查分辨率匹配：期望设置值、相机支持值、lcd分辨率 **/
	if (vfmt.fmt.pix.width != (unsigned int)cam_t.cfg.width ||
    vfmt.fmt.pix.height != (unsigned int)cam_t.cfg.height) {
    LOG_WARN( "resolution adjusted: requested %dx%d, actual %ux%u",
            cam_t.cfg.width, cam_t.cfg.height,
            vfmt.fmt.pix.width, vfmt.fmt.pix.height);
	}

	if (vfmt.fmt.pix.width != lcd_var_screeninfo.xres ||
    vfmt.fmt.pix.height != lcd_var_screeninfo.yres) {
    LOG_WARN( "camera %ux%u != lcd %ux%u, image will not fill screen",
            vfmt.fmt.pix.width, vfmt.fmt.pix.height,
            lcd_var_screeninfo.xres, lcd_var_screeninfo.yres);
	}
	
	/** 请求30fps **/
	struct v4l2_streamparm parm;
	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(cam_t.fd, VIDIOC_G_PARM, &parm) == 0 && 
		(parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)){
		parm.parm.capture.timeperframe.numerator= 1;
		parm.parm.capture.timeperframe.denominator = 30;
		
		if (ioctl(cam_t.fd, VIDIOC_S_PARM, &parm) < 0)
			LOG_WARN("S_PARM 30fps failed: %s", strerror(errno));
		else {
			if(ioctl(cam_t.fd, VIDIOC_G_PARM, &parm) == 0)
				LOG_INFO("capture fps: %u/%u",parm.parm.capture.timeperframe.numerator,
			 parm.parm.capture.timeperframe.denominator);
			else
				LOG_WARN("get PARM after set fail");
		}
	}
	else
		LOG_WARN("driver does not support timeperframe");	
	
	/* 申请帧缓冲 */
	struct v4l2_requestbuffers reqbuffer;
    memset(&reqbuffer, 0, sizeof(struct v4l2_requestbuffers));
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.count = cam_t.cfg.buffers;
	reqbuffer.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(cam_t.fd, VIDIOC_REQBUFS, &reqbuffer);	
	if(ret < 0) {
       LOG_ERROR("Unable to allocate buffers");
		return -1;
    }
	
	/* 映射buffer */
	 for(i = 0; i < cam_t.cfg.buffers; i++) {
		struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(cam_t.fd, VIDIOC_QUERYBUF, &buf);
        if(ret < 0) {
            LOG_ERROR("Unable to query buffer");
			return -1;
        }
		cam_t.buf_length[i] = buf.length;
        cam_t.mem[i] = mmap(0 /* start anywhere */ ,
                          buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam_t.fd,
                          buf.m.offset);
        if(cam_t.mem[i] == MAP_FAILED) {
            LOG_ERROR("Unable to map buffer");
			return -1;
        }
    }
	 
	/* 申请到的帧缓冲放入队列 */
	for(i = 0; i < cam_t.cfg.buffers; ++i) {		
		struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(cam_t.fd, VIDIOC_QBUF, &buf);
        if(ret < 0) {
            LOG_ERROR("line: %d,Unable to queue buffer",__LINE__);
			for (i = 0; i < cam_t.cfg.buffers; i++) {
	        	munmap(cam_t.mem[i], cam_t.buf_length[i]);
			}
			return -1;
        }
    }
	return 0;
}

/* 开始采集数据 */
int camera_start(void){
	int ret = 0;
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	ret = ioctl(cam_t.fd, VIDIOC_STREAMON, &type);
	if(ret < 0) {
		LOG_ERROR("Unable to start capture");
		return -1;
	}
	return 0;
}

/* 获得采集数据 */
int camera_dqueue(struct v4l2_buffer *buf_p){
	int ret;
 	struct v4l2_buffer buf;
	
    memset(&buf, 0, sizeof(struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(cam_t.fd, VIDIOC_DQBUF, &buf);
    if(ret < 0) {
		LOG_ERROR("%s, line: %d ,dequeue buffer: %s",__FUNCTION__,__LINE__,strerror(errno));
		return -1;
    }	
	*buf_p = buf;
	return 0;
}

/* buffer放回队列 */
int camera_queue(struct v4l2_buffer *buf_p){
	int ret;
	struct v4l2_buffer buf = *buf_p;
	
	ret = ioctl(cam_t.fd, VIDIOC_QBUF, &buf);
    if(ret < 0) {
        LOG_ERROR("%s, line: %d ,Unable to queue buffer",__FUNCTION__,__LINE__);
		return -1;
    }
	return 0;
}

/* 停止采集 */
int camera_stop(void){
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	 if(ioctl(cam_t.fd, VIDIOC_STREAMOFF, &type) != 0) {
		 LOG_ERROR("%s, line: %d , stop capture",__FUNCTION__,__LINE__);
		 return -1;
 	}
	 return 0;
}

void camera_deinit(void){
	int i;
	for (i = 0; i < cam_t.cfg.buffers; i++) {
	    munmap(cam_t.mem[i], cam_t.buf_length[i]);
	}
	close(cam_t.fd);
}
