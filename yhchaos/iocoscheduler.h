#ifndef __YHCHAOS_IOCOSCHEDULER_H__
#define __YHCHAOS_IOCOSCHEDULER_H__

#include "coscheduler.h"
#include "timed_coroutine.h"

namespace yhchaos {

/**
 * @brief 基于Epoll的IO协程调度器
 */
class IOCoScheduler : public CoScheduler, public TimedCoroutineManager {
public:
    typedef std::shared_ptr<IOCoScheduler> ptr;
    typedef RWMtx RWMtxType;

    /**
     * @brief IO事件
     */
    enum FdEvent {
        /// 无事件
        NONE    = 0x0,
        /// 读事件(EPOLLIN)
        READ    = 0x1,
        /// 写事件(EPOLLOUT)
        WRITE   = 0x4,
    };
private:
    /**
     * @brief Sock事件上下文类{读写事件上下文},里面存储了每个socket句柄感兴趣的事件(读、写、读写)
     * 以及该事件触发后需要执行的协程(回调函数)，该协程通过指定的协程调度器执行
     */
    struct FileContext {
        typedef Mtx MtxType;
        /**
         * @brief 事件上下文类
         */
        struct FdEventContext {
            /// 事件执行的调度器
            CoScheduler* coscheduler = nullptr;
            /// 事件协程
            Coroutine::ptr coroutine;
            /// 事件的回调函数
            std::function<void()> cb;
        };

        /**
         * @brief 获取事件上下文类
         * @param[in] event 事件类型
         * @return 返回对应事件的上线文
         */
        FdEventContext& getContext(FdEvent event);

        /**
         * @brief 重置事件上下文
         * @param[in, out] ctx 待重置的上下文类
         */
        void resetContext(FdEventContext& ctx);

        /**
         * @brief 触发事件，并将该事件从fd中删除
         * @param[in] event 事件类型，将函数或者coroutine添加到m_coroutines
         */
        void triggerFdEvent(FdEvent event);

        /// 读事件上下文
        FdEventContext read;
        /// 写事件上下文
        FdEventContext write;
        /// 事件关联的句柄
        int fd = 0;
        /// 当前的事件
        FdEvent events = NONE;
        /// 事件的Mtx
        MtxType mutex;
    };

public:
    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否将调用线程包含进去
     * @param[in] name 调度器的名称
     */
    IOCoScheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    /**
     * @brief 析构函数
     */
    ~IOCoScheduler();

    /**
     * @brief 添加事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @param[in] cb 事件回调函数
     * @return 添加成功返回0,失败返回-1
     * @details 先从m_fundContexts中获取fd对应的FileContext，然后将fd和对应的事件类型添加到epoll中，然后将事件回调函数添加到fdContext中
     * 如果cb不为空，则回调函数为cb，如果为空，则fdContext中的FdEventContext::coroutine为调用当前函数的coroutine
     */
    int addFdEvent(int fd, FdEvent event, std::function<void()> cb = nullptr);

    /**
     * @brief 删除事件,从epoll中删除事件，但是不会触发事件，从对应fd的FileContext中删除该事件并reset对应的FdEventContext
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 不会触发事件
     */
    bool delFdEvent(int fd, FdEvent event);

    /**
     * @brief 取消事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 如果事件存在则触发事件
     */
    bool cancelFdEvent(int fd, FdEvent event);

    /**
     * @brief 取消某句柄的所有事件
     * @param[in] fd socket句柄
     */
    bool cancelAll(int fd);

    /**
     * @brief 返回当前的IOCoScheduler
     */
    static IOCoScheduler* GetThis();
protected:
    void tick() override;
    bool stopping() override;
    void idle() override;
    void onTimedCoroutineInsertedAtFront() override;

    /**
     * @brief 重置socket句柄上下文的容器大小
     * @param[in] size 容量大小
     */
    void contextResize(size_t size);

    /**
     * @brief 判断是否可以停止
     * @param[out] timeout 最近要出发的定时器事件间隔
     * @return 返回是否可以停止
     */
    bool stopping(uint64_t& timeout);
private:
    /// epoll 文件句柄
    int m_epfd = 0;
    /// pipe 文件句柄，用于唤醒子线程，处理定时器任务
    int m_tickFds[2];
    /// 当前等待执行的事件数量，正在监听的事件总和
    std::atomic<size_t> m_pendingFdEventCount = {0};
    /// IOCoScheduler的Mtx
    RWMtxType m_mutex;
    /// socket事件上下文的容器
    std::vector<FileContext*> m_fundContexts;
};

}

#endif
