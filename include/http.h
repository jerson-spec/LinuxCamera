#ifndef __HTTP_H_
#define __HTTP_H_

#include "camera.h"
#include "config.h"

/* 建 socket / bind / listen，返回监听 fd，失败返回 -1 */
int http_init(void);

/* accept 一个客户端，返回连接 fd，失败返回 -1 */
int http_accept(int SocketServer_fd);

/*
 * 处理一个客户端：收请求、解析、拍照、回响应。
 * client_fd 由本函数负责关闭。
 * 返回 0 表示处理完成（200/404 都算），-1 表示出错。
 */
int http_serve_snapshot(int client_fd, camera_t *cam, const app_config_t *cfg);

/* 关闭监听 fd */
void http_close(int SocketServer_fd);

#endif
