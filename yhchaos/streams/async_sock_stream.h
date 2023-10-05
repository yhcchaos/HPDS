#ifndef __YHCHAOS_STREAMS_ASYNC_SOCK_STREAM_H__
#define __YHCHAOS_STREAMS_ASYNC_SOCK_STREAM_H__

#include "sock_stream.h"
#include <list>
#include <unordered_map>
#include <boost/any.hpp>
//实现了异步读写
namespace yhchaos {
// 每个AsyncSockStream对象对应一个socket连接，每个连接都有一个sn，每个sn对应一个Ctx对象
class AsyncSockStream : public SockStream
                         ,public std::enable_shared_from_this<AsyncSockStream> {
public:
    typedef std::shared_ptr<AsyncSockStream> ptr;
    typedef yhchaos::RWMtx RWMtxType;
    typedef std::function<bool(AsyncSockStream::ptr)> connect_callback;
    typedef std::function<void(AsyncSockStream::ptr)> disconnect_callback;
    //Ctx.result
    enum Error {
        OK = 0,
        TIMEOUT = -1,
        IO_ERROR = -2,
        NOT_CONNECT = -3,
    };
    AsyncSockStream(Sock::ptr sock, bool owner = true);
    void setWorker(yhchaos::IOCoScheduler* v) { m_worker = v;}
    yhchaos::IOCoScheduler* getWorker() const { return m_worker;}

    void setIOCoScheduler(yhchaos::IOCoScheduler* v) { m_iomanager = v;}
    yhchaos::IOCoScheduler* getIOCoScheduler() const { return m_iomanager;}

    bool isAutoConnect() const { return m_autoConnect;}
    void setAutoConnect(bool v) { m_autoConnect = v;}
    //返回值为bool
    connect_callback getConnectCb() const { return m_connectCb;}
    //返回值为void
    disconnect_callback getDisconnectCb() const { return m_disconnectCb;}
    void setConnectCb(connect_callback v) { m_connectCb = v;}
    void setDisconnectCb(disconnect_callback v) { m_disconnectCb = v;}

    template<class T>
    void setData(const T& v) { m_data = v;}

    template<class T>
    T getData() const {
        try {
            return boost::any_cast<T>(m_data);
        } catch (...) {
        }
        return T();
    }
    virtual bool start();
    virtual void close() override;
protected:
    //doWrite用来发送request的
    struct SendCtx {
    public:
        typedef std::shared_ptr<SendCtx> ptr;
        virtual ~SendCtx() {}

        virtual bool doSend(AsyncSockStream::ptr stream) = 0;
    };
    //doRead用来接收和回复的
    //这个类还是个抽象类，ctx和sn是一一对应的关系，按照sn=ctx存储在散列m_ctxs中,sn也存储在ctx中
    struct Ctx : public SendCtx {
    public:
        typedef std::shared_ptr<Ctx> ptr;
        virtual ~Ctx() {}
        Ctx();
        //这里的sn与protocol中的Req和ResPonse类中的sn是一一对应的
        uint32_t sn;//0
        uint32_t timeout;//0
        uint32_t result;//0
        //是否超时
        bool timed;//false

        CoScheduler* coscheduler;//nullptr
        Coroutine::ptr coroutine;

        TimedCoroutine::ptr timer;
        /**
         * @brief 在coscheduler中执行coroutine，处理收到的DPRsp
        */
        virtual void doRsp();
    };

protected:
    //循环执行do_Recv，读并处理报文
    virtual void doRead();
    //应为m_sem中的m_concurrency的值为0,所以该函数必定停止当前协程的执行，转到主协程
    //doWrite协程会在m_queue为空且执行enqueue时进行调度唤醒
    virtual void doWrite();
    //将doRead函数添加到m_iomanager的任务队列中进行调度
    virtual void startRead();
    //将doWrite函数添加到m_iomanager的任务队列中进行调度
    virtual void startWrite();
    virtual void onTimeOut(Ctx::ptr ctx);
    //在doRead中执行，发挥一个ctx
    virtual Ctx::ptr doRecv() = 0;

    Ctx::ptr getCtx(uint32_t sn);
    Ctx::ptr getAndDelCtx(uint32_t sn);

    template<class T>
    std::shared_ptr<T> getCtxAs(uint32_t sn) {
        auto ctx = getCtx(sn);
        if(ctx) {
            return std::dynamic_pointer_cast<T>(ctx);
        }
        return nullptr;
    }

    template<class T>
    std::shared_ptr<T> getAndDelCtxAs(uint32_t sn) {
        auto ctx = getAndDelCtx(sn);
        if(ctx) {
            return std::dynamic_pointer_cast<T>(ctx);
        }
        return nullptr;
    }

    bool addCtx(Ctx::ptr ctx);
    //如果m_queue为空，则挂起协程
    bool enqueue(SendCtx::ptr ctx);
    bool innerClose();
    bool waitCoroutine();
protected:
    //默认初始化为0，表示m_queue中是否有资源，m_sem.m_concurrency的最大值为1，
    //表示m_queue中有ctx资源，m_sem.m_concurrency的最小值为0，表示m_queue中没有ctx资源。
    yhchaos::CoroutineSem m_sem;
    //初始化为2，保证同时只有两个协程在运行
    yhchaos::CoroutineSem m_waitSem;
    //保护m_queue
    RWMtxType m_queueMtx;
    //发送队列
    std::list<SendCtx::ptr> m_queue;
    //保护m_ctxs
    RWMtxType m_mutex;
    //接收队列，一个sn对用一个ctx，每个ctx中都存储了对应的sn
    std::unordered_map<uint32_t, Ctx::ptr> m_ctxs;
    uint32_t m_sn;//0
    bool m_autoConnect;//false
    //start函数的重启计时器
    yhchaos::TimedCoroutine::ptr m_timer;
    yhchaos::IOCoScheduler* m_iomanager;//doRead和doWrite在这个线程池中运行
    //处理Req和Notify的调度器
    yhchaos::IOCoScheduler* m_worker;//nullptr
    //在startRead和startWrite执行之前需要执行的函数，应该是完成读写前的准备工作
    connect_callback m_connectCb;
    //在关闭socket前执行的函数，完成关闭前的清理工作
    disconnect_callback m_disconnectCb;

    boost::any m_data;
};

class AsyncSockStreamManager {
public:
    typedef yhchaos::RWMtx RWMtxType;
    typedef AsyncSockStream::connect_callback connect_callback;
    typedef AsyncSockStream::disconnect_callback disconnect_callback;

    AsyncSockStreamManager();
    virtual ~AsyncSockStreamManager() {}

    void add(AsyncSockStream::ptr stream);
    void clear();
    //将m_datas中的AsyncSockStream对象和替换为streams中的AsyncSockStream对象,关闭原先m_datas中的AsyncSockStream对象的连接
    void setClient(const std::vector<AsyncSockStream::ptr>& streams);
    //返回m_idx+1对应的AsyncSockStream对象
    AsyncSockStream::ptr get();
    template<class T>
    std::shared_ptr<T> getAs() {
        auto rt = get();
        if(rt) {
            return std::dynamic_pointer_cast<T>(rt);
        }
        return nullptr;
    }

    connect_callback getConnectCb() const { return m_connectCb;}
    disconnect_callback getDisconnectCb() const { return m_disconnectCb;}
    void setConnectCb(connect_callback v);
    void setDisconnectCb(disconnect_callback v);
private:
    //保护m_datas
    RWMtxType m_mutex;
    //m_datas中AsyncSockStream的个数
    uint32_t m_size;//0

    uint32_t m_idx;//0
    std::vector<AsyncSockStream::ptr> m_datas;
    //m_datas中AsyncSockStream的连接回调函数,所有AsyncSockStream对象的连接回调函数都是一样的
    connect_callback m_connectCb;
    //m_datas中AsyncSockStream的断开连接回调函数,所有AsyncSockStream对象的断开连接回调函数都是一样的
    disconnect_callback m_disconnectCb;
};

}

#endif
