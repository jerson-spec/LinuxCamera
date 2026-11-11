#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <linux/videodev2.h>

#define CAMERA_MAX_BUFS 8

/*
 * 一路 V4L2 采集的状态。
 * 由调用者持有，所有接口都显式传入，模块内不保留全局状态。
 */
typedef struct {
	int          fd;
	unsigned int width;
	unsigned int height;
	unsigned int pixfmt;
	unsigned int nbufs;                        /* 已 mmap 的 buffer 数 */
	unsigned int buf_length[CAMERA_MAX_BUFS];
	void        *mem[CAMERA_MAX_BUFS];
	int          streaming;
} camera_t;

/* "YUYV" / "MJPG" / "MJPEG" -> V4L2_PIX_FMT_*，不认识返回 0 */
unsigned int camera_pixfmt_from_string(const char *s);

/* 打开设备并检查 QUERYCAP；成功后 cam->fd >= 0 */
int camera_open(camera_t *cam, const char *dev);

/* S_FMT 并回读；必须在不 streaming 时调用 */
int camera_set_format(camera_t *cam, unsigned int width, unsigned int height,
                      unsigned int pixfmt);

/* 请求帧率，尽力而为；驱动不支持时返回 -1，调用者可忽略 */
int camera_set_fps(camera_t *cam, unsigned int num, unsigned int den);

/* REQBUFS + mmap + QBUF + STREAMON；失败会回滚 */
int camera_start(camera_t *cam, unsigned int nbufs);

/* 阻塞取一帧（调用前应先 poll），成功后 buf 可直接用 */
int camera_dequeue(camera_t *cam, struct v4l2_buffer *buf);

/* 归还一帧 */
int camera_queue(camera_t *cam, struct v4l2_buffer *buf);

/* STREAMOFF + munmap + REQBUFS(0)；可重复调用 */
int camera_stop(camera_t *cam);

/* 停流、释放 buffer、关闭 fd；可重复调用 */
void camera_close(camera_t *cam);

#endif
