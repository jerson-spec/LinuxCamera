#ifndef __SNAPSHOT_H_
#define __SNAPSHOT_H_

#include <stddef.h>

#include "camera.h"
#include "config.h"

/*
 * 抓一帧 MJPEG 到内存。
 *
 * 成功：*outdata 指向 malloc 出来的缓冲区，调用者负责 free()；
 * 返回 0 成功，1 失败但预览已恢复，-1 预览恢复失败。
 */
int camera_grab_jpeg(camera_t *cam, const app_config_t *cfg,
                     unsigned char **outdata, size_t *out_len);

#endif
