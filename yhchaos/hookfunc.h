#ifndef __YHCHAOS_HOOKFUNC_H__
#define __YHCHAOS_HOOKFUNC_H__

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

namespace yhchaos {
    /**
     * @brief 当前线程是否hook，如果线程被hook了，那么线程中的所有的函数都会替换成我们自己的实现
     */
    bool is_hook_enable();
    /**
     * @brief 设置当前线程的hook状态
     */
    void set_hook_enable(bool flag);
}

extern "C" {

//sleep
typedef unsigned int (*sleep_func)(unsigned int seconds);
extern sleep_func sleep_fun;

typedef int (*usleep_func)(useconds_t usec);
extern usleep_func usleep_fun;

typedef int (*nanosleep_func)(const struct timespec *req, struct timespec *rem);
extern nanosleep_func nanosleep_fun;

//socket
typedef int (*socket_func)(int domain, int type, int protocol);
extern socket_func socket_fun;

typedef int (*connect_func)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern connect_func connect_fun;

typedef int (*accept_func)(int s, struct sockaddr *addr, socklen_t *addrlen);
extern accept_func accept_fun;

//read
typedef ssize_t (*read_func)(int fd, void *buf, size_t count);
extern read_func read_fun;

typedef ssize_t (*readv_func)(int fd, const struct iovec *iov, int iovcnt);
extern readv_func readv_fun;

typedef ssize_t (*recv_func)(int sockfd, void *buf, size_t len, int flags);
extern recv_func recv_fun;

typedef ssize_t (*recvfrom_func)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
extern recvfrom_func recvfrom_fun;

typedef ssize_t (*recvmsg_func)(int sockfd, struct msghdr *msg, int flags);
extern recvmsg_func recvmsg_fun;

//write
typedef ssize_t (*write_func)(int fd, const void *buf, size_t count);
extern write_func write_fun;

typedef ssize_t (*writev_func)(int fd, const struct iovec *iov, int iovcnt);
extern writev_func writev_fun;

typedef ssize_t (*send_func)(int s, const void *msg, size_t len, int flags);
extern send_func send_fun;

typedef ssize_t (*sendto_func)(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
extern sendto_func sendto_fun;

typedef ssize_t (*sendmsg_func)(int s, const struct msghdr *msg, int flags);
extern sendmsg_func sendmsg_fun;

typedef int (*close_func)(int fd);
extern close_func close_fun;

//
typedef int (*fcntl_func)(int fd, int cmd, ... /* arg */ );
extern fcntl_func fcntl_fun;

typedef int (*ioctl_func)(int d, unsigned long int request, ...);
extern ioctl_func ioctl_fun;

typedef int (*getsockopt_func)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
extern getsockopt_func getsockopt_fun;

typedef int (*setsockopt_func)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern setsockopt_func setsockopt_fun;

extern int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms);

}

#endif
