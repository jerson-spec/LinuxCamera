#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "http.h"
#include "snapshot.h"
#include "camera.h"
#include "config.h"
#include "log.h"

#define HTTP_PORT    8888
#define HTTP_BACKLOG 10

/*
*	循环发，直到把 len 个字节全部发完
*/
static int send_all(int fd, const void *buf, size_t len)
{
	const unsigned char *p = buf;

	while (len > 0) {
		ssize_t n = send(fd, p, len, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

int http_init(void)
{
	int fd;
	int on = 1;
	struct sockaddr_in addr;

	/* 客户端提前断开时，send 会产生 SIGPIPE；忽略掉，只看返回值 */
	signal(SIGPIPE, SIG_IGN);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		LOG_ERROR("http: socket: %s", strerror(errno));
		return -1;
	}

	/* 程序退出后立刻重启，不会报 Address already in use */
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		LOG_WARN("http: SO_REUSEADDR: %s", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(HTTP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERROR("http: bind %d: %s", HTTP_PORT, strerror(errno));
		close(fd);
		return -1;
	}

	if (listen(fd, HTTP_BACKLOG) < 0) {
		LOG_ERROR("http: listen: %s", strerror(errno));
		close(fd);
		return -1;
	}

	LOG_INFO("http: listening on 0.0.0.0:%d", HTTP_PORT);
	return fd;
}

int http_accept(int SocketServer_fd)
{
	int cfd;
	struct sockaddr_in peer;
	socklen_t peer_len = sizeof(peer);
	struct timeval tv = { 5, 0 };

	cfd = accept(SocketServer_fd, (struct sockaddr *)&peer, &peer_len);
	if (cfd < 0) {
		if (errno != EINTR)
			LOG_WARN("http: accept: %s", strerror(errno));
		return -1;
	}

	/* 浏览器连上后迟迟不发请求时，recv 最多等 5 秒 */
	setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	LOG_INFO("http: client %s:%d", inet_ntoa(peer.sin_addr),
	         ntohs(peer.sin_port));

	return cfd;
}

int http_serve_snapshot(int client_fd, camera_t *cam, const app_config_t *cfg)
{
	char req[2048];
	char method[16];
	char path[256];
	char hdr[256];
	/* 响应头 */
	static const char not_found[] =
		"HTTP/1.1 404 Not Found\r\n"
		"Content-Length: 0\r\n"
		"Connection: close\r\n"
		"\r\n";
	static const char server_error[] =
		"HTTP/1.1 500 Internal Server Error\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 15\r\n"
		"Connection: close\r\n"
		"\r\n"
		"snapshot failed";
	unsigned char *jpeg = NULL;
	size_t jpeg_len = 0;
	ssize_t n;
	int hlen;
	int ret = 0;

	/* 1. 收请求（GET 的请求头一般一次就能收完） */
	n = recv(client_fd, req, sizeof(req) - 1, 0);
	if (n <= 0) {
		LOG_WARN("http: recv: %s", n == 0 ? "peer closed" : strerror(errno));
		ret = -1;
		goto out;
	}
	req[n] = '\0';

	/* 2. 解析第一行：GET /snapshot.jpg HTTP/1.1 */
	if (sscanf(req, "%15s %255s", method, path) != 2) {
		LOG_WARN("http: bad request");
		ret = -1;
		goto out;
	}
	LOG_INFO("http: %s %s", method, path);

	if (strcmp(method, "GET") != 0 || strcmp(path, "/snapshot.jpg") != 0) {
		send_all(client_fd, not_found, sizeof(not_found) - 1);
		goto out;
	}

	/* 3. 抓一帧 JPEG 到内存 */
	if (camera_grab_jpeg(cam, cfg, &jpeg, &jpeg_len) != 0) {
		LOG_ERROR("http: grab jpeg failed");
		send_all(client_fd, server_error, sizeof(server_error) - 1);
		ret = -1;
		goto out;
	}

	/* 4. 先发响应头（文本），再发 JPEG 字节（二进制） */
	hlen = snprintf(hdr, sizeof(hdr),
	                "HTTP/1.1 200 OK\r\n"
	                "Content-Type: image/jpeg\r\n"
	                "Content-Length: %lu\r\n"
	                "Cache-Control: no-store\r\n"
	                "Connection: close\r\n"
	                "\r\n",
	                (unsigned long)jpeg_len);
	if (hlen < 0 || hlen >= (int)sizeof(hdr)) {
		ret = -1;
		goto out;
	}

	if (send_all(client_fd, hdr, (size_t)hlen) < 0 ||
	    send_all(client_fd, jpeg, jpeg_len) < 0) {
		LOG_WARN("http: send: %s", strerror(errno));
		ret = -1;
	}

	LOG_INFO("http: sent %lu bytes", (unsigned long)jpeg_len);

out:
	free(jpeg);
	close(client_fd);
	return ret;
}

void http_close(int SocketServer_fd)
{
	if (SocketServer_fd >= 0)
		close(SocketServer_fd);
}
