#include "iocoscheduler.h"
#include "macro.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

enum EpollCtlOp {
};

static std::ostream& operator<< (std::ostream& os, const EpollCtlOp& op) {
    switch((int)op) {
#define XX(ctl) \
        case ctl: \
            return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
        default:
            return os << (int)op;
    }
#undef XX
}

static std::ostream& operator<< (std::ostream& os, EPOLL_EVENTS events) {
    if(!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E) \
    if(events & E) { \
        if(!first) { \
            os << "|"; \
        } \
        os << #E; \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOCoScheduler::FileContext::FdEventContext& IOCoScheduler::FileContext::getContext(IOCoScheduler::FdEvent event) {
    switch(event) {
        case IOCoScheduler::READ:
            return read;
        case IOCoScheduler::WRITE:
            return write;
        default:
            YHCHAOS_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOCoScheduler::FileContext::resetContext(FdEventContext& ctx) {
    ctx.coscheduler = nullptr;
    ctx.coroutine.reset();
    ctx.cb = nullptr;
}

void IOCoScheduler::FileContext::triggerFdEvent(IOCoScheduler::FdEvent event) {
    //YHCHAOS_LOG_INFO(g_logger) << "fd=" << fd
    //    << " triggerFdEvent event=" << event
    //    << " events=" << events;
    YHCHAOS_ASSERT(events & event);
    //if(YHCHAOS_UNLIKELY(!(event & event))) {
    //    return;
    //}
    events = (FdEvent)(events & ~event);
    FdEventContext& ctx = getContext(event);
    if(ctx.cb) {
        ctx.coscheduler->coschedule(&ctx.cb);
    } else {
        ctx.coscheduler->coschedule(&ctx.coroutine);
    }
    ctx.coscheduler = nullptr;
    return;
}
//创建一个调度器，以非阻塞方式讲pipe[0]加入epoll事件表中，初始化m_fdContexts，启动调度器
IOCoScheduler::IOCoScheduler(size_t threads, bool use_caller, const std::string& name)
    :CoScheduler(threads, use_caller, name) {
    m_epfd = epoll_create(5000);
    YHCHAOS_ASSERT(m_epfd > 0);

    int rt = pipe(m_tickFds);
    YHCHAOS_ASSERT(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = m_tickFds[0];

    rt = fcntl(m_tickFds[0], F_SETFL, O_NONBLOCK);
    YHCHAOS_ASSERT(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickFds[0], &event);
    YHCHAOS_ASSERT(!rt);

    contextResize(32);

    start();
}

IOCoScheduler::~IOCoScheduler() {
    stop();
    close(m_epfd);
    close(m_tickFds[0]);
    close(m_tickFds[1]);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOCoScheduler::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(!m_fdContexts[i]) {
            m_fdContexts[i] = new FileContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOCoScheduler::addFdEvent(int fd, FdEvent event, std::function<void()> cb) {
    FileContext* fd_ctx = nullptr;
    RWMtxType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMtxType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    FileContext::MtxType::Lock lock2(fd_ctx->mutex);
    //这个事件已经有人加过了，一般情况下，一个句柄一般不会重复加同一种类型的事件，这就意味着至少有两个不同的线程在操作同一个句柄
    if(YHCHAOS_UNLIKELY(fd_ctx->events & event)) {
        YHCHAOS_LOG_ERROR(g_logger) << "addFdEvent assert fd=" << fd
                    << " event=" << (EPOLL_EVENTS)event
                    << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        //要求加的事件和已经加的事件不一样
        YHCHAOS_ASSERT(!(fd_ctx->events & event));
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        YHCHAOS_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
            << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    ++m_pendingFdEventCount;
    fd_ctx->events = (FdEvent)(fd_ctx->events | event);
    FileContext::FdEventContext& event_ctx = fd_ctx->getContext(event);
    YHCHAOS_ASSERT(!event_ctx.coscheduler
                && !event_ctx.coroutine
                && !event_ctx.cb);
    //在当前线程加入事件，就要用当前线程的协程调度器来执行该事件的回调coroutine
    event_ctx.coscheduler = CoScheduler::GetThis();
    if(cb) {
        event_ctx.cb.swap(cb);
    } else {
        //设置为调用addFdEvent的coroutine，当线程池中的某个函数执行coroutine.swapIn的时候，会跳转到该coroutine中调用swapOut或者YieldToHold的下一条语句执行
        event_ctx.coroutine = Coroutine::GetThis();
        YHCHAOS_ASSERT2(event_ctx.coroutine->getState() == Coroutine::EXEC
                      ,"state=" << event_ctx.coroutine->getState());
    }
    return 0;
}

bool IOCoScheduler::delFdEvent(int fd, FdEvent event) {
    RWMtxType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FileContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FileContext::MtxType::Lock lock2(fd_ctx->mutex);
    //如果要移除的事件和当前的事件不一致，就返回false，也就是说要移除的事件必须包含在当前的事件中
    if(YHCHAOS_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }
    //将当前event从fd_ctx->events中移除
    FdEvent new_events = (FdEvent)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        YHCHAOS_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    --m_pendingFdEventCount;
    fd_ctx->events = new_events;
    FileContext::FdEventContext& event_ctx = fd_ctx->getContext(event);
    fd_ctx->resetContext(event_ctx);
    return true;
}

bool IOCoScheduler::cancelFdEvent(int fd, FdEvent event) {
    RWMtxType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FileContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FileContext::MtxType::Lock lock2(fd_ctx->mutex);
    if(YHCHAOS_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    FdEvent new_events = (FdEvent)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        YHCHAOS_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    fd_ctx->triggerFdEvent(event);
    --m_pendingFdEventCount;
    return true;
}

bool IOCoScheduler::cancelAll(int fd) {
    RWMtxType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FileContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FileContext::MtxType::Lock lock2(fd_ctx->mutex);
    if(!fd_ctx->events) {
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        YHCHAOS_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    if(fd_ctx->events & READ) {
        fd_ctx->triggerFdEvent(READ);
        --m_pendingFdEventCount;
    }
    if(fd_ctx->events & WRITE) {
        fd_ctx->triggerFdEvent(WRITE);
        --m_pendingFdEventCount;
    }

    YHCHAOS_ASSERT(fd_ctx->events == 0);
    return true;
}

IOCoScheduler* IOCoScheduler::GetThis() {
    return dynamic_cast<IOCoScheduler*>(CoScheduler::GetThis());
}

void IOCoScheduler::tick() {
    if(!hasIdleCppThreads()) {
        return;
    }
    int rt = write(m_tickFds[1], "T", 1);
    YHCHAOS_ASSERT(rt == 1);
}

bool IOCoScheduler::stopping(uint64_t& timeout) {
    timeout = getNextTimedCoroutine();
    return timeout == ~0ull//没有计时任务了
        && m_pendingFdEventCount == 0//没有要监听的事件了
        && CoScheduler::stopping();

}

bool IOCoScheduler::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}
//空闲协程
void IOCoScheduler::idle() {
    YHCHAOS_LOG_DEBUG(g_logger) << "idle";
    const uint64_t MAX_EVNETS = 256;
    epoll_event* events = new epoll_event[MAX_EVNETS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){
        delete[] ptr;
    });

    while(true) {
        uint64_t next_timeout = 0;
        //下一个定时器事件的时间=next_timeout
        if(YHCHAOS_UNLIKELY(stopping(next_timeout))) {
            YHCHAOS_LOG_INFO(g_logger) << "name=" << getName()
                                     << " idle stopping exit";
            break;
        }

        int rt = 0;
        do {
            static const int MAX_TIMEOUT = 3000;
            if(next_timeout != ~0ull) {
                next_timeout = (int)next_timeout > MAX_TIMEOUT
                                ? MAX_TIMEOUT : next_timeout;
            } else {//没有计时任务
                next_timeout = MAX_TIMEOUT;
            }
            //next_timeout=min(next_timeout, MAX_TIMEOUT)
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            //如果是被中断了，就继续执行
            if(rt < 0 && errno == EINTR) {
            } else {
                break;
            }
        } while(true);
        //当被唤醒的时候，就把已经超时的定时器任务添加到m_coroutines中，这些任务的thread=-1，任意的线程都可以处理它
        std::vector<std::function<void()> > cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) {
            //YHCHAOS_LOG_DEBUG(g_logger) << "on timer cbs.size=" << cbs.size();
            coschedule(cbs.begin(), cbs.end());
            cbs.clear();
        }

        //if(YHCHAOS_UNLIKELY(rt == MAX_EVNETS)) {
        //    YHCHAOS_LOG_INFO(g_logger) << "epoll wait events=" << rt;
        //}

        for(int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];
            //被其他线程给tick唤醒了
            if(event.data.fd == m_tickFds[0]) {
                uint8_t dummy[256];
                while(read(m_tickFds[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            FileContext* fd_ctx = (FileContext*)event.data.ptr;
            FileContext::MtxType::Lock lock(fd_ctx->mutex);
            if(event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if(event.events & EPOLLIN) {
                real_events |= READ;
            }
            if(event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if((fd_ctx->events & real_events) == NONE) {
                continue;
            }
            //将监听到的事件删除
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if(rt2) {
                YHCHAOS_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                    << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            //YHCHAOS_LOG_INFO(g_logger) << " fd=" << fd_ctx->fd << " events=" << fd_ctx->events
            //                         << " real_events=" << real_events;
            if(real_events & READ) {
                fd_ctx->triggerFdEvent(READ);
                --m_pendingFdEventCount;
            }
            if(real_events & WRITE) {
                fd_ctx->triggerFdEvent(WRITE);
                --m_pendingFdEventCount;
            }
        }

        Coroutine::ptr cur = Coroutine::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        raw_ptr->swapOut();
    }
}

void IOCoScheduler::onTimedCoroutineInsertedAtFront() {
    tick();
}

}
