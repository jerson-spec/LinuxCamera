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
#include <poll.h>

#include "http.h"
#include "snapshot.h"
#include "camera.h"
#include "config.h"
#include "log.h"

#define HTTP_PORT    8888
#define HTTP_BACKLOG 10

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
static const char multipart[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
	"Cache-Control: no-store\r\n"
	"Connection: close\r\n"
	"\r\n";

/* 修改：每个 MJPEG 分片的头，循环里只发它，不再重复发整个 HTTP 响应头 */
#define PART_HDR_FMT \
	"--frame\r\n" \
	"Content-Type: image/jpeg\r\n" \
	"Content-Length: %u\r\n" \
	"\r\n"


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
	struct timeval rcv = { 5, 0 };
	struct timeval snd = { 5, 0 };		/* 修改：加发送超时，慢客户端不卡死 */

	cfd = accept(SocketServer_fd, (struct sockaddr *)&peer, &peer_len);
	if (cfd < 0) {
		if (errno != EINTR)
			LOG_WARN("http: accept: %s", strerror(errno));
		return -1;
	}

	/* 浏览器连上后迟迟不发请求时，recv 最多等 5 秒 */
	setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
	/* 修改：客户端不收数据时，send 最多卡 5 秒就返回 */
	setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));

	LOG_INFO("http: client %s:%d", inet_ntoa(peer.sin_addr),
	         ntohs(peer.sin_port));

	return cfd;
}

/*
 * 修改：原 http_serve_snapshot 拆出来的“拍照 + 回图”部分。
 * 请求已经由 http_serve() 收好并解析过，这里只管干活，不 close。
 */
static int serve_snapshot_body(int client_fd, camera_t *cam, const app_config_t *cfg)
{
	char hdr[256];
	unsigned char *jpeg = NULL;
	size_t jpeg_len = 0;
	int hlen;
	int ret = 0;

	/* 抓一帧 JPEG 到内存 */
	if (camera_grab_jpeg(cam, cfg, &jpeg, &jpeg_len) != 0) {
		LOG_ERROR("http: grab jpeg failed");
		send_all(client_fd, server_error, sizeof(server_error) - 1);
		ret = -1;
		goto out;
	}

	/* 先发响应头（文本），再发 JPEG 字节（二进制） */
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
	return ret;
}

/*
 * 修改：新增“连续 MJPEG 流”。
 * - HTTP 响应头只发一次；
 * - 摄像头只切一次 MJPG，循环里 DQBUF → send → QBUF；
 * - 退出时无论什么原因，都把摄像头切回 YUYV 并重新起流。
 */
static int serve_stream_body(int client_fd, camera_t *cam,
                             const app_config_t *cfg, volatile sig_atomic_t *stop)
{
	struct v4l2_buffer buf;
	struct pollfd pfd;
	char hdr[160];
	int hlen;
	int ret = -1;

	/* 1. HTTP 响应头只发这一次 */
	if (send_all(client_fd, multipart, sizeof(multipart) - 1) < 0) {
		LOG_WARN("http: stream send header: %s", strerror(errno));
		return -1;
	}

	/* 2. 切到 MJPG，只切这一次 */
	camera_stop(cam);
	if (camera_set_format(cam, cam->width, cam->height, V4L2_PIX_FMT_MJPEG) < 0)
		goto restore;
	if (camera_start(cam, cfg->buffers) < 0)
		goto restore;

	LOG_INFO("http: stream start %ux%u", cam->width, cam->height);
	ret = 0;

	while (stop == NULL || !*stop) {
		char tmp;
		ssize_t pr;
		int poll_ret;

		/*
		 * 修改：浏览器关标签页时会发 FIN。
		 * 用非阻塞 peek 看一眼，返回 0 说明对端已关闭，直接退出循环。
		 * 返回 -1/EAGAIN 是正常的“暂时没数据”。
		 */
		pr = recv(client_fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
		if (pr == 0) {
			LOG_INFO("http: stream client closed");
			break;
		}

		pfd.fd = cam->fd;
		pfd.events = POLLIN;
		poll_ret = poll(&pfd, 1, 1000);	/* 修改：1 秒出来查一次 stop */
		if (poll_ret < 0) {
			if (errno == EINTR)
				continue;
			LOG_WARN("http: stream poll: %s", strerror(errno));
			ret = -1;
			break;
		}
		if (poll_ret == 0)
			continue;

		if (camera_dequeue(cam, &buf) < 0) {
			ret = -1;
			break;
		}
		if (buf.bytesused == 0) {
			camera_queue(cam, &buf);
			continue;
		}

		/* 3. 发分片头 + JPEG + 结尾 CRLF */
		hlen = snprintf(hdr, sizeof(hdr), PART_HDR_FMT, buf.bytesused);
		if (hlen < 0 || hlen >= (int)sizeof(hdr) ||
		    send_all(client_fd, hdr, (size_t)hlen) < 0 ||
		    send_all(client_fd, cam->mem[buf.index], buf.bytesused) < 0 ||
		    send_all(client_fd, "\r\n", 2) < 0) {
			camera_queue(cam, &buf);
			LOG_WARN("http: stream send: %s", strerror(errno));
			ret = -1;
			break;
		}
		camera_queue(cam, &buf);
	}

restore:
	/* 4. 恢复预览：stop → YUYV → 帧率 → start */
	camera_stop(cam);
	if (camera_set_format(cam, cam->width, cam->height, V4L2_PIX_FMT_YUYV) < 0)
		ret = -1;
	camera_set_fps_if_needed(cam, 1, 30);
	if (camera_start(cam, cfg->buffers) < 0)
		ret = -1;

	LOG_INFO("http: stream end (ret=%d)", ret);
	return ret;
}

/*
 * 修改：统一入口。
 * 只 recv 一次、解析一次，再按路径分给 snapshot 或 stream。
 * client_fd 由本函数关闭。
 */
int http_serve(int client_fd, camera_t *cam, const app_config_t *cfg,
               volatile sig_atomic_t *stop)
{
	char req[2048];
	char method[16];
	char path[256];
	ssize_t n;
	int ret = 0;

	/***** 处理空连接丢帧 *****/
	struct pollfd p = { .fd = client_fd, .events = POLLIN };

	if (poll(&p, 1, 300) <= 0) {
	    LOG_INFO("http: idle client, close");
	    close(client_fd);
	    return -1;
	}
	/*************************/
	n = recv(client_fd, req, sizeof(req) - 1, 0);
	if (n <= 0) {
		LOG_WARN("http: recv: %s", n == 0 ? "peer closed" : strerror(errno));
		ret = -1;
		goto out;
	}
	req[n] = '\0';

	if (sscanf(req, "%15s %255s", method, path) != 2) {
		LOG_WARN("http: bad request");
		ret = -1;
		goto out;
	}
	LOG_INFO("http: %s %s", method, path);

	if (strcmp(method, "GET") != 0) {
		send_all(client_fd, not_found, sizeof(not_found) - 1);
		goto out;
	}

	if (strcmp(path, "/snapshot.jpg") == 0)
		ret = serve_snapshot_body(client_fd, cam, cfg);
	else if (strcmp(path, "/stream") == 0)
		ret = serve_stream_body(client_fd, cam, cfg, stop);
	else
		send_all(client_fd, not_found, sizeof(not_found) - 1);

out:
	close(client_fd);
	return ret;
}

void http_close(int SocketServer_fd)
{
	if (SocketServer_fd >= 0)
		close(SocketServer_fd);
}
