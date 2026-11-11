#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <linux/videodev2.h>

#include "camera.h"
#include "log.h"

unsigned int camera_pixfmt_from_string(const char *s)
{
	if (s == NULL)
		return 0;
	if (!strcasecmp(s, "MJPG") || !strcasecmp(s, "MJPEG"))
		return V4L2_PIX_FMT_MJPEG;
	if (!strcasecmp(s, "YUYV"))
		return V4L2_PIX_FMT_YUYV;
	return 0;
}

/* 解映射所有 buffer，并把计数器归零 */
static void camera_unmap_buffers(camera_t *cam)
{
	unsigned int i;

	for (i = 0; i < cam->nbufs; i++) {
		if (cam->mem[i] != NULL) {
			munmap(cam->mem[i], cam->buf_length[i]);
			cam->mem[i] = NULL;
		}
		cam->buf_length[i] = 0;
	}
	cam->nbufs = 0;
}

int camera_open(camera_t *cam, const char *dev)
{
	struct v4l2_capability cap;
	int ret;

	if (cam == NULL || dev == NULL)
		return -1;

	memset(cam, 0, sizeof(*cam));
	cam->fd = -1;

	cam->fd = open(dev, O_RDWR);
	if (cam->fd < 0) {
		LOG_ERROR("open %s: %s", dev, strerror(errno));
		return -1;
	}

	ret = ioctl(cam->fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		LOG_ERROR("QUERYCAP %s: %s", dev, strerror(errno));
		goto fail;
	}
	if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
		LOG_ERROR("%s: video capture not supported", dev);
		goto fail;
	}
	if ((cap.capabilities & V4L2_CAP_STREAMING) == 0) {
		LOG_ERROR("%s: streaming i/o not supported", dev);
		goto fail;
	}

	LOG_INFO("camera: %s (%s)", dev, cap.card);
	return 0;

fail:
	close(cam->fd);
	cam->fd = -1;
	return -1;
}

int camera_set_format(camera_t *cam, unsigned int width, unsigned int height,
                      unsigned int pixfmt)
{
	struct v4l2_format vfmt;
	int ret;

	if (cam == NULL || cam->fd < 0)
		return -1;
	if (cam->streaming) {
		LOG_ERROR("set_format while streaming, caller must stop first");
		return -1;
	}
	if (width == 0 || height == 0 || pixfmt == 0) {
		LOG_ERROR("set_format: bad args %ux%u fmt=0x%08x", width, height, pixfmt);
		return -1;
	}

	memset(&vfmt, 0, sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = width;
	vfmt.fmt.pix.height = height;
	vfmt.fmt.pix.pixelformat = pixfmt;

	ret = ioctl(cam->fd, VIDIOC_S_FMT, &vfmt);
	if (ret < 0) {
		LOG_ERROR("S_FMT %ux%u fmt=0x%08x: %s",
		          width, height, pixfmt, strerror(errno));
		return -1;
	}

	if (vfmt.fmt.pix.pixelformat != pixfmt) {
		LOG_ERROR("S_FMT: driver returned pixfmt 0x%08x, wanted 0x%08x",
		          vfmt.fmt.pix.pixelformat, pixfmt);
		return -1;
	}

	if (vfmt.fmt.pix.width != width || vfmt.fmt.pix.height != height) {
		LOG_WARN("resolution adjusted: requested %ux%u, actual %ux%u",
		         width, height, vfmt.fmt.pix.width, vfmt.fmt.pix.height);
	}

	/* YUYV 的 stride 必须是 width*2，代码里按这个前提做地址计算。
	 * MJPG 的 bytesperline 通常是 0，不检查。 */
	if (pixfmt == V4L2_PIX_FMT_YUYV &&
	    vfmt.fmt.pix.bytesperline != vfmt.fmt.pix.width * 2) {
		LOG_ERROR("YUYV bytesperline %u != width*2 %u",
		          vfmt.fmt.pix.bytesperline, vfmt.fmt.pix.width * 2);
		return -1;
	}

	cam->width  = vfmt.fmt.pix.width;
	cam->height = vfmt.fmt.pix.height;
	cam->pixfmt = vfmt.fmt.pix.pixelformat;

	LOG_INFO("format: %ux%u fmt=0x%08x bytesperline=%u sizeimage=%u",
	         cam->width, cam->height, cam->pixfmt,
	         vfmt.fmt.pix.bytesperline, vfmt.fmt.pix.sizeimage);
	return 0;
}

int camera_set_fps_if_needed(camera_t *cam, unsigned int num, unsigned int den)
{
	struct v4l2_streamparm parm;
	
	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if(ioctl(cam->fd, VIDIOC_G_PARM, &parm)== 0 && parm.parm.capture.timeperframe.numerator == num 
		&& parm.parm.capture.timeperframe.denominator == den){
		return 0;
	}
	
	return camera_set_fps(cam, num ,den);
}

int camera_set_fps(camera_t *cam, unsigned int num, unsigned int den)
{
	struct v4l2_streamparm parm;

	if (cam == NULL || cam->fd < 0 || num == 0 || den == 0)
		return -1;

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(cam->fd, VIDIOC_G_PARM, &parm) < 0) {
		LOG_WARN("G_PARM: %s", strerror(errno));
		return -1;
	}
	if ((parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == 0) {
		LOG_WARN("driver does not support timeperframe");
		return -1;
	}

	parm.parm.capture.timeperframe.numerator   = num;
	parm.parm.capture.timeperframe.denominator = den;

	if (ioctl(cam->fd, VIDIOC_S_PARM, &parm) < 0) {
		LOG_WARN("S_PARM %u/%u failed: %s", num, den, strerror(errno));
		return -1;
	}

	/* 回读驱动实际接受的值 */
	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(cam->fd, VIDIOC_G_PARM, &parm) == 0) {
		LOG_INFO("capture fps: %u/%u",
		         parm.parm.capture.timeperframe.numerator,
		         parm.parm.capture.timeperframe.denominator);
	} else {
		LOG_WARN("G_PARM after S_PARM: %s", strerror(errno));
	}

	return 0;
}

int camera_start(camera_t *cam, unsigned int nbufs)
{
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	unsigned int i;

	if (cam == NULL || cam->fd < 0)
		return -1;
	if (cam->streaming) {
		LOG_WARN("camera_start: already streaming");
		return 0;
	}
	if (nbufs < 2 || nbufs > CAMERA_MAX_BUFS) {
		LOG_ERROR("camera_start: bad nbufs %u", nbufs);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.count  = nbufs;
	req.type   = type;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0) {
		LOG_ERROR("REQBUFS(%u): %s", nbufs, strerror(errno));
		return -1;
	}
	if (req.count < 2) {
		LOG_ERROR("REQBUFS: got only %u buffers", req.count);
		goto fail;
	}

	for (i = 0; i < req.count; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type   = type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = i;

		if (ioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) {
			LOG_ERROR("QUERYBUF[%u]: %s", i, strerror(errno));
			goto fail;
		}

		cam->buf_length[i] = buf.length;
		cam->mem[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
		                   MAP_SHARED, cam->fd, buf.m.offset);
		if (cam->mem[i] == MAP_FAILED) {
			cam->mem[i] = NULL;
			LOG_ERROR("mmap[%u]: %s", i, strerror(errno));
			goto fail;
		}
		cam->nbufs = i + 1;
	}

	for (i = 0; i < cam->nbufs; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type   = type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = i;

		if (ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) {
			LOG_ERROR("QBUF[%u]: %s", i, strerror(errno));
			goto fail;
		}
	}

	if (ioctl(cam->fd, VIDIOC_STREAMON, &type) < 0) {
		LOG_ERROR("STREAMON: %s", strerror(errno));
		goto fail;
	}

	cam->streaming = 1;
	LOG_INFO("streaming: %u buffers", cam->nbufs);
	return 0;

fail:
	camera_stop(cam);
	return -1;
}

int camera_dequeue(camera_t *cam, struct v4l2_buffer *buf)
{
	if (cam == NULL || buf == NULL || cam->fd < 0)
		return -1;

	memset(buf, 0, sizeof(*buf));
	buf->type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;

	if (ioctl(cam->fd, VIDIOC_DQBUF, buf) < 0) {
		if (errno != EAGAIN)
			LOG_ERROR("DQBUF: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int camera_queue(camera_t *cam, struct v4l2_buffer *buf)
{
	if (cam == NULL || buf == NULL || cam->fd < 0)
		return -1;

	buf->type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;

	if (ioctl(cam->fd, VIDIOC_QBUF, buf) < 0) {
		LOG_ERROR("QBUF: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int camera_stop(camera_t *cam)
{
	struct v4l2_requestbuffers req;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret = 0;

	if (cam == NULL || cam->fd < 0)
		return -1;

	if (cam->streaming) {
		if (ioctl(cam->fd, VIDIOC_STREAMOFF, &type) < 0) {
			LOG_ERROR("STREAMOFF: %s", strerror(errno));
			ret = -1;
		}
		cam->streaming = 0;
	}

	camera_unmap_buffers(cam);

	/* 让驱动释放内部 buffer，否则改格式/改分辨率时会被旧 buffer 卡住 */
	memset(&req, 0, sizeof(req));
	req.count  = 0;
	req.type   = type;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0)
		LOG_WARN("REQBUFS(0): %s", strerror(errno));

	return ret;
}

void camera_close(camera_t *cam)
{
	if (cam == NULL)
		return;

	if (cam->fd >= 0) {
		if (cam->streaming)
			camera_stop(cam);
		else
			camera_unmap_buffers(cam);
		close(cam->fd);
		cam->fd = -1;
	}
}
