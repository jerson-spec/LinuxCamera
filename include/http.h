#ifndef __HTTP_H_
#define __HTTP_H_

#include <signal.h>

#include "camera.h"
#include "config.h"

/* 建 socket / bind / listen，返回监听 fd，失败返回 -1 */
int http_init(void);

/* accept 一个客户端，返回连接 fd，失败返回 -1 */
int http_accept(int SocketServer_fd);

/*
 * 修改：统一入口。
 * 收请求、解析路径，分发给 /snapshot.jpg 或 /stream。
 * client_fd 由本函数负责关闭。
 * stop 指向主循环的 g_stop，推流时用来退出。
 */
int http_serve(int client_fd, camera_t *cam, const app_config_t *cfg,
               volatile sig_atomic_t *stop);

/* 关闭监听 fd */
void http_close(int SocketServer_fd);

#endif
