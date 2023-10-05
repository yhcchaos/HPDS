#include "async_sock_stream.h"
#include "yhchaos/util.h"
#include "yhchaos/log.h"
#include "yhchaos/macro.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

AsyncSockStream::Ctx::Ctx()
    :sn(0)
    ,timeout(0)
    ,result(0)
    ,timed(false)
    ,coscheduler(nullptr) {
}

void AsyncSockStream::Ctx::doRsp() {
    CoScheduler* scd = coscheduler;
    if(!yhchaos::Atomic::compareAndSwapBool(coscheduler, scd, (CoScheduler*)nullptr)) {
        return;
    }
    if(!scd || !coroutine) {
        return;
    }
    //如果coroutine放在以后某个时间进行调度，则调用此函数的时候取消此定时任务
    if(timer) {
        timer->cancel();
        timer = nullptr;
    }
    if(timed) {
        result = TIMEOUT;
    }
    //立马将coroutine加入coschedule的队列进行调度
    scd->coschedule(&coroutine);
}

AsyncSockStream::AsyncSockStream(Sock::ptr sock, bool owner)
    :SockStream(sock, owner)
    ,m_waitSem(2)
    ,m_sn(0)
    ,m_autoConnect(false)
    ,m_iomanager(nullptr)
    ,m_worker(nullptr) {
}

bool AsyncSockStream::start() {
    if(!m_iomanager) {
        m_iomanager = yhchaos::IOCoScheduler::GetThis();
    }
    if(!m_worker) {
        m_worker = yhchaos::IOCoScheduler::GetThis();
    }

    do {
        //如果m_concurrency<0，则将当前coscheduler和当前coroutine加入队列
        //调度了两个协程
        waitCoroutine();
        //执行start就将start函数的定时器取消掉
        if(m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }

        if(!isConnected()) {
            if(!m_socket->reconnect()) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }
        //连接回调函数
        if(m_connectCb) {
            if(!m_connectCb(shared_from_this())) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        startRead();
        startWrite();
        //表示start函数执行成功
        return true;
    } while(false);
    //表示start函数执行失败，并根据是否重新连接来决定是否在两秒后重启start函数
    if(m_autoConnect) {
        if(m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }
        //2秒后重启start函数，
        m_timer = m_iomanager->addTimedCoroutine(2 * 1000,
                std::bind(&AsyncSockStream::start, shared_from_this()));
    }
    return false;
}

void AsyncSockStream::doRead() {
    try {
        while(isConnected()) {
            auto ctx = doRecv();
            if(ctx) {
                //在coscheduler中调度coroutine
                ctx->doRsp();
            }
        }
    } catch (...) {
        //TODO log
    }

    YHCHAOS_LOG_DEBUG(g_logger) << "doRead out " << this;
    innerClose();
    m_waitSem.notify();

    if(m_autoConnect) {
        m_iomanager->addTimedCoroutine(10, std::bind(&AsyncSockStream::start, shared_from_this()));
    }
}

void AsyncSockStream::doWrite() {
    try {
        while(isConnected()) {
            //加入m_sem的队列中，然后将当前调度器的当前coroutine挂起，转到当前调度器的主协程
            //停止当前协程的执行，转到主协程
            m_sem.wait();
            std::list<SendCtx::ptr> ctxs;
            {
                RWMtxType::WriteLock lock(m_queueMtx);
                m_queue.swap(ctxs);
            }
            auto self = shared_from_this();
            for(auto& i : ctxs) {
                if(!i->doSend(self)) {
                    //出错读写都要关闭
                    innerClose();
                    break;
                }
            }
        }
    } catch (...) {
        //TODO log
    }
    YHCHAOS_LOG_DEBUG(g_logger) << "doWrite out " << this;
    {
        RWMtxType::WriteLock lock(m_queueMtx);
        m_queue.clear();
    }
    m_waitSem.notify();
}

void AsyncSockStream::startRead() {
    m_iomanager->coschedule(std::bind(&AsyncSockStream::doRead, shared_from_this()));
}

void AsyncSockStream::startWrite() {
    m_iomanager->coschedule(std::bind(&AsyncSockStream::doWrite, shared_from_this()));
}

void AsyncSockStream::onTimeOut(Ctx::ptr ctx) {
    {
        RWMtxType::WriteLock lock(m_mutex);
        m_ctxs.erase(ctx->sn);
    }
    ctx->timed = true;
    ctx->doRsp();
}

AsyncSockStream::Ctx::ptr AsyncSockStream::getCtx(uint32_t sn) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_ctxs.find(sn);
    return it != m_ctxs.end() ? it->second : nullptr;
}

AsyncSockStream::Ctx::ptr AsyncSockStream::getAndDelCtx(uint32_t sn) {
    Ctx::ptr ctx;
    RWMtxType::WriteLock lock(m_mutex);
    auto it = m_ctxs.find(sn);
    if(it != m_ctxs.end()) {
        ctx = it->second;
        m_ctxs.erase(it);
    }
    return ctx;
}

bool AsyncSockStream::addCtx(Ctx::ptr ctx) {
    RWMtxType::WriteLock lock(m_mutex);
    m_ctxs.insert(std::make_pair(ctx->sn, ctx));
    return true;
}

bool AsyncSockStream::enqueue(SendCtx::ptr ctx) {
    YHCHAOS_ASSERT(ctx);
    RWMtxType::WriteLock lock(m_queueMtx);
    bool empty = m_queue.empty();
    m_queue.push_back(ctx);
    lock.unlock();
    if(empty) {
        m_sem.notify();
    }
    return empty;
}

bool AsyncSockStream::innerClose() {
    YHCHAOS_ASSERT(m_iomanager == yhchaos::IOCoScheduler::GetThis());
    if(isConnected() && m_disconnectCb) {
        m_disconnectCb(shared_from_this());
    }
    SockStream::close();
    m_sem.notify();
    std::unordered_map<uint32_t, Ctx::ptr> ctxs;
    {
        RWMtxType::WriteLock lock(m_mutex);
        ctxs.swap(m_ctxs);
    }
    {
        RWMtxType::WriteLock lock(m_queueMtx);
        m_queue.clear();
    }
    for(auto& i : ctxs) {
        i.second->result = IO_ERROR;
        i.second->doRsp();
    }
    return true;
}

bool AsyncSockStream::waitCoroutine() {
    m_waitSem.wait();
    m_waitSem.wait();
    return true;
}

void AsyncSockStream::close() {
    m_autoConnect = false;
    CoSchedulerSwitcher ss(m_iomanager);
    if(m_timer) {
        m_timer->cancel();
    }
    SockStream::close();
}

AsyncSockStreamManager::AsyncSockStreamManager()
    :m_size(0)
    ,m_idx(0) {
}

void AsyncSockStreamManager::add(AsyncSockStream::ptr stream) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas.push_back(stream);
    ++m_size;

    if(m_connectCb) {
        stream->setConnectCb(m_connectCb);
    }

    if(m_disconnectCb) {
        stream->setDisconnectCb(m_disconnectCb);
    }
}

void AsyncSockStreamManager::clear() {
    RWMtxType::WriteLock lock(m_mutex);
    for(auto& i : m_datas) {
        i->close();
    }
    m_datas.clear();
    m_size = 0;
}
void AsyncSockStreamManager::setClient(const std::vector<AsyncSockStream::ptr>& streams) {
    auto cs = streams;
    RWMtxType::WriteLock lock(m_mutex);
    cs.swap(m_datas);
    m_size = m_datas.size();
    if(m_connectCb || m_disconnectCb) {
        for(auto& i : m_datas) {
            if(m_connectCb) {
                i->setConnectCb(m_connectCb);
            }
            if(m_disconnectCb) {
                i->setDisconnectCb(m_disconnectCb);
            }
        }
    }
    lock.unlock();

    for(auto& i : cs) {
        i->close();
    }
}

AsyncSockStream::ptr AsyncSockStreamManager::get() {
    RWMtxType::ReadLock lock(m_mutex);
    for(uint32_t i = 0; i < m_size; ++i) {
        auto idx = yhchaos::Atomic::addFetch(m_idx, 1);
        if(m_datas[idx % m_size]->isConnected()) {
            return m_datas[idx % m_size];
        }
    }
    return nullptr;
}

void AsyncSockStreamManager::setConnectCb(connect_callback v) {
    m_connectCb = v;
    RWMtxType::WriteLock lock(m_mutex);
    for(auto& i : m_datas) {
        i->setConnectCb(m_connectCb);
    }
}

void AsyncSockStreamManager::setDisconnectCb(disconnect_callback v) {
    m_disconnectCb = v;
    RWMtxType::WriteLock lock(m_mutex);
    for(auto& i : m_datas) {
        i->setDisconnectCb(m_disconnectCb);
    }
}

}
