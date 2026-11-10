#ifndef _CAMERA_H_
#define _CAMERA_H_

#include "config.h"
#include "log.h"
#include <linux/videodev2.h>

#define camera_max_bufs 8

typedef struct {
	int w;
	int h;
	int fd;
	app_config_t cfg;
	log_level_t log_level;
	char cfg_path[256];
	int buf_length[camera_max_bufs];
	void *mem[camera_max_bufs];
	
}camera_t;

int camera_init(char* camera_cfg_path , app_config_t *camera_cfg_p, camera_t *cam_t_p);
int camera_start(void);
int camera_dqueue(struct v4l2_buffer *buf_p);
int camera_queue(struct v4l2_buffer *buf_p);
int camera_stop(void);
void camera_deinit(void);

#endif
