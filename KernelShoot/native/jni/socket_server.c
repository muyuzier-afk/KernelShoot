/*
 * KernelShoot - socket_server.c
 * LocalSocket Server (Linux 抽象命名空间)
 *
 * 抽象命名空间约定:
 *   sockaddr_un.sun_path[0] = '\0'
 *   其后跟名字字节 (不计末尾 \0)
 *   bind 长度 = offsetof(sockaddr_un, sun_path) + 1 + strlen(name)
 *
 * 协议: 帧长度前缀 (4 字节大端 uint32) + JSON 负载
 *   方便切割, 避免 TCP 粘包/LocalSocket 半包问题
 */
#define _GNU_SOURCE  /* glibc 下声明 accept4; bionic 无影响 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <arpa/inet.h>   /* htonl/ntohl */

#define KS_EPOLL_EVENTS 8
#define KS_CONN_BACKLOG 8

/* 创建并绑定抽象命名空间监听 socket, 返回 fd 或 -1 */
int ks_server_create(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        KS_LOGE("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* 抽象命名空间: sun_path[0]='\0', 之后是名字 */
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, KS_SOCKET_NAME, sizeof(addr.sun_path) - 2);

    socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(KS_SOCKET_NAME);

    /* SO_REUSEADDR 防止残留 bind 失败 */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(fd, (struct sockaddr *)&addr, addrlen) < 0) {
        KS_LOGE("bind(%s) failed: %s", KS_SOCKET_NAME, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, KS_CONN_BACKLOG) < 0) {
        KS_LOGE("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    KS_LOGI("server listening on abstract namespace @%s", KS_SOCKET_NAME);
    return fd;
}

/* 客户端连接: 连到抽象命名空间, 返回 fd 或 -1 */
int ks_client_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, KS_SOCKET_NAME, sizeof(addr.sun_path) - 2);

    socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(KS_SOCKET_NAME);

    /* 5 秒超时保护 (避免 Java 侧无限阻塞) */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&addr, addrlen) < 0) {
        KS_LOGE("connect(@%s) failed: %s", KS_SOCKET_NAME, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* ---------- 帧读写 (长度前缀) ---------- */

/* 阻塞写入完整 buffer, 返回写入字节数或 -1 */
static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return (int)done;
}

/* 阻塞读取完整 buffer, 返回读取字节数或 -1 */
static int read_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return (int)done;
}

/* 服务端发送响应: 写入 4 字节长度 + JSON */
int ks_server_send_resp(int fd, const char *json, size_t json_len) {
    uint32_t net_len = htonl((uint32_t)json_len);
    if (write_all(fd, &net_len, 4) < 0) return -1;
    if (write_all(fd, json, json_len) < 0) return -1;
    return 0;
}

/* 客户端发送请求并接收响应, resp_out 由调用方释放(free) */
int ks_client_request(const char *req_json, size_t req_len,
                      char **resp_out, size_t *resp_len_out) {
    *resp_out = NULL;
    if (resp_len_out) *resp_len_out = 0;

    int fd = ks_client_connect();
    if (fd < 0) return -1;

    /* 发请求 */
    uint32_t net_len = htonl((uint32_t)req_len);
    if (write_all(fd, &net_len, 4) < 0 || write_all(fd, req_json, req_len) < 0) {
        close(fd);
        return -1;
    }

    /* 读响应 */
    uint32_t resp_len_net = 0;
    if (read_all(fd, &resp_len_net, 4) < 0) {
        close(fd);
        return -1;
    }
    uint32_t resp_len = ntohl(resp_len_net);
    if (resp_len == 0 || resp_len > KS_RESP_MAX_LEN) {
        close(fd);
        return -1;
    }

    char *resp = (char *)malloc(resp_len + 1);
    if (!resp) { close(fd); return -1; }
    if (read_all(fd, resp, resp_len) < 0) {
        free(resp);
        close(fd);
        return -1;
    }
    resp[resp_len] = '\0';

    close(fd);
    *resp_out = resp;
    if (resp_len_out) *resp_len_out = resp_len;
    return 0;
}

/* ---------- 服务端事件循环 ----------
 * 调用方传入处理回调; server_create 已返回 listen fd
 * 非截图期间 epoll_wait 阻塞, CPU 占用 0%
 */
typedef int (*ks_req_handler_t)(int client_fd, const char *req, size_t req_len);

void ks_server_loop(int listen_fd, ks_req_handler_t handler) {
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        KS_LOGE("epoll_create1 failed: %s", strerror(errno));
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    struct epoll_event events[KS_EPOLL_EVENTS];
    KS_LOGI("entering epoll loop");

    for (;;) {
        int n = epoll_wait(epfd, events, KS_EPOLL_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            KS_LOGE("epoll_wait failed: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                /* 新连接 */
                int cfd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
                if (cfd < 0) {
                    if (errno != EINTR) KS_LOGW("accept4 failed: %s", strerror(errno));
                    continue;
                }
                struct epoll_event cev;
                cev.events = EPOLLIN;
                cev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
            } else {
                /* 已连接 socket 有数据 */
                int cfd = events[i].data.fd;
                uint32_t len_net = 0;
                if (read_all(cfd, &len_net, 4) < 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
                    close(cfd);
                    continue;
                }
                uint32_t rlen = ntohl(len_net);
                if (rlen == 0 || rlen > KS_REQ_MAX_LEN) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
                    close(cfd);
                    continue;
                }
                char *req = (char *)malloc(rlen + 1);
                if (!req) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
                    close(cfd);
                    continue;
                }
                if (read_all(cfd, req, rlen) < 0) {
                    free(req);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
                    close(cfd);
                    continue;
                }
                req[rlen] = '\0';

                /* 调用业务处理 (返回响应由 handler 直接 write 给客户端) */
                handler(cfd, req, rlen);

                free(req);
                epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
                close(cfd);
            }
        }
    }
    close(epfd);
}
