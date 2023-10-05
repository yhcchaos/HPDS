#include "hookfunc.h"
#include <dlfcn.h>

#include "appconfig.h"
#include "log.h"
#include "coroutine.h"
#include "iocoscheduler.h"
#include "file_manager.h"
#include "macro.h"

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
namespace yhchaos {

static yhchaos::AppConfigVar<int>::ptr g_tcp_connect_timeout =
    yhchaos::AppConfig::SearchFor("tcp.connect.timeout", 5000, "tcp connect timeout");

static thread_local bool t_hook_enable = false;
//将头文件的函数指针指向相应的系统函数。
#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)

void hook_init() {
    static bool is_inited = false;
    if(is_inited) {
        return;
    }
#define XX(name) name ## _fun = (name ## _func)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX
}

static uint64_t s_connect_timeout = -1;//5000ms
struct _HookIniter {
    _HookIniter() {
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue();

        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
                YHCHAOS_LOG_INFO(g_logger) << "tcp connect timeout changed from "
                                         << old_value << " to " << new_value;
                s_connect_timeout = new_value;
        });
    }
};

static _HookIniter s_hook_initer;

bool is_hook_enable() {
    return t_hook_enable;
}

void set_hook_enable(bool flag) {
    t_hook_enable = flag;
}

}

struct timer_info {
    int cancelled = 0;//0:没有取消，1：取消
};

//do_io(s, accept_fun, "accept", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, addr, addrlen);
//如果没有在fdMgr上找到fd，就直接执行原版本
//如果找到了就执行超时版本，超时时间都在fdMgr中的m_recvTimeout和m_sendTimeout中设置了
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_func_name,
        uint32_t event, int timeout_so, Args&&... args) {
    //调用原始版本
    if(!yhchaos::t_hook_enable) {
        return fun(fd, std::forward<Args>(args)...);
    }

    yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(fd);
    if(!ctx) {
        return fun(fd, std::forward<Args>(args)...);
    }

    if(ctx->isClose()) {
        errno = EBADF;
        return -1;
    }
    //如果用户设置了非阻塞或者不是socket，那么直接调用原始版本
    if(!ctx->isSock() || ctx->getUserNonblock()) {
        return fun(fd, std::forward<Args>(args)...);
    }

    uint64_t to = ctx->getTimeout(timeout_so);
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
    //如果是socket，则以非阻塞模式调用原始版本，如果返回-1并且错误码是EAGAIN，则表示当前操作会阻塞，需要注册epoll事件，然后切换协程，等待epoll唤醒
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    while(n == -1 && errno == EINTR) {
        n = fun(fd, std::forward<Args>(args)...);
    }
    if(n == -1 && errno == EAGAIN) {
        yhchaos::IOCoScheduler* iom = yhchaos::IOCoScheduler::GetThis();
        yhchaos::TimedCoroutine::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        if(to != (uint64_t)-1) {
            timer = iom->addConditionTimedCoroutine(to, [winfo, fd, iom, event]() {
                auto t = winfo.lock();
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                iom->cancelFdEvent(fd, (yhchaos::IOCoScheduler::FdEvent)(event));
            }, winfo);
        }

        int rt = iom->addFdEvent(fd, (yhchaos::IOCoScheduler::FdEvent)(event));
        if(YHCHAOS_UNLIKELY(rt)) {//rt=-1,addFdEvent出错了
            YHCHAOS_LOG_ERROR(g_logger) << hook_func_name << " addFdEvent("
                << fd << ", " << event << ")";
            if(timer) {
                timer->cancel();
            }
            return -1;
        } else {
            yhchaos::Coroutine::YieldToHold();
            if(timer) {
                timer->cancel();
            }
            if(tinfo->cancelled) {
                errno = tinfo->cancelled;
                return -1;
            }
            //如果epoll发生相应事件，就再次取读该套接字，获得相应的数据
            goto retry;
        }
    }
    
    return n;
}


extern "C" {
#define XX(name) name ## _func name ## _fun = nullptr;
    HOOK_FUN(XX);
#undef XX
//调用sleep的coroutine会挂起seconds秒，期间会切换到其他的协程执行
unsigned int sleep(unsigned int seconds) {
    if(!yhchaos::t_hook_enable) {
        return sleep_fun(seconds);
    }

    yhchaos::Coroutine::ptr coroutine = yhchaos::Coroutine::GetThis();
    yhchaos::IOCoScheduler* iom = yhchaos::IOCoScheduler::GetThis();
    //把IOCoScheduler::coschedule加入到定时器中，并作为该定时任务的函数
    iom->addTimedCoroutine(seconds * 1000, std::bind((void(yhchaos::CoScheduler::*)
            (yhchaos::Coroutine::ptr, int thread))&yhchaos::IOCoScheduler::coschedule
            ,iom, coroutine, -1));
    yhchaos::Coroutine::YieldToHold();
    return 0;
}

int usleep(useconds_t usec) {
    if(!yhchaos::t_hook_enable) {
        return usleep_fun(usec);
    }
    yhchaos::Coroutine::ptr coroutine = yhchaos::Coroutine::GetThis();
    yhchaos::IOCoScheduler* iom = yhchaos::IOCoScheduler::GetThis();
    iom->addTimedCoroutine(usec / 1000, std::bind((void(yhchaos::CoScheduler::*)
            (yhchaos::Coroutine::ptr, int thread))&yhchaos::IOCoScheduler::coschedule
            ,iom, coroutine, -1));
    yhchaos::Coroutine::YieldToHold();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if(!yhchaos::t_hook_enable) {
        return nanosleep_fun(req, rem);
    }

    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 /1000;
    yhchaos::Coroutine::ptr coroutine = yhchaos::Coroutine::GetThis();
    yhchaos::IOCoScheduler* iom = yhchaos::IOCoScheduler::GetThis();
    iom->addTimedCoroutine(timeout_ms, std::bind((void(yhchaos::CoScheduler::*)
            (yhchaos::Coroutine::ptr, int thread))&yhchaos::IOCoScheduler::coschedule
            ,iom, coroutine, -1));
    yhchaos::Coroutine::YieldToHold();
    return 0;
}
//如果挂钩子了，就将fd添加到FdMgr中，没挂钩子就不加
int socket(int domain, int type, int protocol) {
    if(!yhchaos::t_hook_enable) {
        return socket_fun(domain, type, protocol);
    }
    int fd = socket_fun(domain, type, protocol);
    if(fd == -1) {
        return fd;
    }
    //为当前fd创建一个fd_ctx,并添加到fd_manager中的队列
    yhchaos::FdMgr::GetInstance()->get(fd, true);
    return fd;
}
//只针对socket类型的fd，如果是其他类型的fd，就直接调用原始版本，并返回connect的返回值
//在timeout_ms这段时间内这个协程是挂起的，如果hook了就定时，如果没有hook就直接返回connect返回值
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if(!yhchaos::t_hook_enable) {
        return connect_fun(fd, addr, addrlen);
    }
    yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(fd);
    if(!ctx || ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSock()) {
        return connect_fun(fd, addr, addrlen);
    }
    //如果用户将fd设置为非阻塞，则直接调用connect_fun
    if(ctx->getUserNonblock()) {
        return connect_fun(fd, addr, addrlen);
    }

    int n = connect_fun(fd, addr, addrlen);
    //直接连接成功
    if(n == 0) {
        return 0;
    /* The socket is nonblocking and the connection cannot be
              completed immediately.  (UNIX domain sockets failed with
              EAGAIN instead.)  It is possible to select(2) or poll(2)
              for completion by selecting the socket for writing.  After
              select(2) indicates writability, use getsockopt(2) to read
              the SO_ERROR option at level SOL_SOCKET to determine
              whether connect() completed successfully (SO_ERROR is
              zero) or unsuccessfully (SO_ERROR is one of the usual
              error codes listed here, explaining the reason for the
              failure).
    */
   //注意我们当建立fd_ctx的时候就将socketfd设置为非阻塞了
    } else if(n != -1 || errno != EINPROGRESS) {
        return n;
    }
    //下面表示n==-1 && errno==EINPROGRESS
    yhchaos::IOCoScheduler* iom = yhchaos::IOCoScheduler::GetThis();
    yhchaos::TimedCoroutine::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    if(timeout_ms != (uint64_t)-1) {
        timer = iom->addConditionTimedCoroutine(timeout_ms, [winfo, fd, iom]() {//timer的引用计数为2，计时器队列中1个，返回值1个
                auto t = winfo.lock();
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                iom->cancelFdEvent(fd, yhchaos::IOCoScheduler::WRITE);
        }, winfo);
    }
    //会把当前协程作为事件处理函数
    int rt = iom->addFdEvent(fd, yhchaos::IOCoScheduler::WRITE);
    if(rt == 0) {
        //转到主协程执行
        yhchaos::Coroutine::YieldToHold();
        //从计时器队列上删除该timer，并且将m_cb设为nullptr
        if(timer) {
            timer->cancel();
        }
        //表示超时了，也就是说一直到timeout_ms都没有写事件发生，表示没有连接上，设置errno和返回-1
        if(tinfo->cancelled) {
            errno = tinfo->cancelled;
            return -1;
        }
    } else {//rt=-1，addFdEvent出错，取消定时器
        if(timer) {
            timer->cancel();
        }
        YHCHAOS_LOG_ERROR(g_logger) << "connect addFdEvent(" << fd << ", WRITE) error";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
        return -1;
    }
    if(!error) {//error=0,表示未出错执行成功
        return 0;
    } else {
        errno = error;
        return -1;
    }
}

//自动调用超时版本
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect_with_timeout(sockfd, addr, addrlen, yhchaos::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int fd = do_io(s, accept_fun, "accept", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, addr, addrlen);
    if(fd >= 0) {
        //不管用不用超时版本都要将连接socket添加到FdMgr
        yhchaos::FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_fun, "read", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_fun, "readv", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_fun, "recv", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_fun, "recvfrom", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_fun, "recvmsg", yhchaos::IOCoScheduler::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_fun, "write", yhchaos::IOCoScheduler::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_fun, "writev", yhchaos::IOCoScheduler::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_fun, "send", yhchaos::IOCoScheduler::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_fun, "sendto", yhchaos::IOCoScheduler::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_fun, "sendmsg", yhchaos::IOCoScheduler::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd) {
    if(!yhchaos::t_hook_enable) {
        return close_fun(fd);
    }

    yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(fd);
    //在fdMgr上获得了fd，就取消fd上所有的事件，并唤醒对应的事件处理函数，然后从fdMgr队列删除fd，然后执行close，如果没有获得fd，就直接执行close
    if(ctx) {
        auto iom = yhchaos::IOCoScheduler::GetThis();
        if(iom) {
            iom->cancelAll(fd);
        }
        yhchaos::FdMgr::GetInstance()->del(fd);
    }
    return close_fun(fd);
}

int fcntl(int fd, int cmd, ... /* arg */ ) {
    va_list va;
    va_start(va, cmd);
    switch(cmd) {
        case F_SETFL:
            {
                int arg = va_arg(va, int);
                va_end(va);
                yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(fd);
                //不是socket或者已关闭或者fdMgr不存在fd
                if(!ctx || ctx->isClose() || !ctx->isSock()) {
                    return fcntl_fun(fd, cmd, arg);
                }
                //这里必定是socket，设置用户的非阻塞标志，根据用户是否设置非阻塞设置UserNonblock
                ctx->setUserNonblock(arg & O_NONBLOCK);
                if(ctx->getSysNonblock()) {
                    arg |= O_NONBLOCK;
                } else {
                    arg &= ~O_NONBLOCK;
                }
                return fcntl_fun(fd, cmd, arg);
            }
            break;
        case F_GETFL:
            {
                va_end(va);
                int arg = fcntl_fun(fd, cmd);
                yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSock()) {
                    return arg;
                }
                //这里必定是socket
                if(ctx->getUserNonblock()) {
                    return arg | O_NONBLOCK;
                } else {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_fun(fd, cmd, arg); 
            }
            break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_fun(fd, cmd);
            }
            break;
        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_fun(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_fun(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_fun(fd, cmd);
    }
}

int ioctl(int d, unsigned long int request, ...) {
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if(FIONBIO == request) {
    /*这段代码中的 `bool user_nonblock = !!*(int*)arg;` 是将 `arg` 解析为 `int` 类型指针，然后使用两次逻辑非运算符 `!!` 将 `int` 转换为 `bool` 类型。
    让我来解释这个表达式的每一步：
    1. `*(int*)arg`：这里将 `arg` 当作指向 `int` 类型的指针。通过 `(int*)` 强制类型转换，将 `arg` 解释为指向 `int` 类型的指针，然后使用 `*` 解引用这个指针，获取其指向的 `int` 值。

    2. `!!*(int*)arg`：在这里，首先使用逻辑非运算符 `!` 对 `*(int*)arg` 的值取反。然后，再次使用逻辑非运算符 `!` 对前一步的结果取反。这两次逻辑非运算实际上是为了将 `int` 类型的值转换为 `bool` 类型。

    所以，整个表达式的含义是将传递给函数的 `arg` 参数（实际类型为 `int*`）解释为一个 `int` 值，然后将这个 `int` 值转换为对应的 `bool` 值。这个 `bool` 值表示是否设置非阻塞模式。

    值得注意的是，这种技巧可以用来将整数转换为 `bool` 类型，将非零的整数转换为 `true`，将零转换为 `false`。在这段代码中，它的目的是用来设置非阻塞模式的标志。*/
        bool user_nonblock = !!*(int*)arg;
        yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(d);
        if(!ctx || ctx->isClose() || !ctx->isSock()) {
            return ioctl_fun(d, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_fun(d, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    return getsockopt_fun(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    if(!yhchaos::t_hook_enable) {
        return setsockopt_fun(sockfd, level, optname, optval, optlen);
    }
    if(level == SOL_SOCKET) {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            yhchaos::FileContext::ptr ctx = yhchaos::FdMgr::GetInstance()->get(sockfd);
            if(ctx) {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_fun(sockfd, level, optname, optval, optlen);
}

}
