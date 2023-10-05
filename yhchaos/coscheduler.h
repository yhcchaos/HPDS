#ifndef __YHCHAOS_COSCHEDULER_H__
#define __YHCHAOS_COSCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include <iostream>
#include "coroutine.h"
#include "cpp_thread.h"

namespace yhchaos {

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器
 *          内部有一个线程池,支持协程在线程池里面切换
 */
class CoScheduler {
public:
    typedef std::shared_ptr<CoScheduler> ptr;
    typedef Mtx MtxType;

    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否使用当前调用线程,是否使用当前调用线程作为协程执行线程。默认为 true。
     * @param[in] name 协程调度器名称
     */
    CoScheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    /**
     * @brief 析构函数，只有在m_stopping=true的时候才会设置
     */
    virtual ~CoScheduler();

    /**
     * @brief 返回协程调度器名称
     */
    const std::string& getName() const { return m_name;}

    /**
     * @brief 返回当前线程所属的调度器
     */
    static CoScheduler* GetThis();

    /**
     * @brief 返回当前协程调度器的调度协程，对于主线程，就是m_rootCoroutine
     * 对于子线程则是Coroutine的私有默认构造函数创建的协程（主协程）
     */
    static Coroutine* GetMainCoroutine();

    /**
     * @brief 启动协程调度器，只有m_stopping=true的时候才会执行
     */
    void start();

    /**
     * @brief 停止协程调度器
     */
    void stop();

    /**
     * @brief 调度协程，如果加入前协程队列m_coroutines为空，则执行一次tick()函数，否则不执行
     * @param[in] fc 协程或函数
     * @param[in] thread 协程执行的线程id,-1标识任意线程
     */
    template<class CoroutineOrCb>
    void coschedule(CoroutineOrCb fc, int thread = -1) {
        //如果m_coroutines为空，则tick，即加入的协程或函数是m_coroutines中的第一个
        bool need_tick = false;
        {
            MtxType::Lock lock(m_mutex);
            need_tick = coscheduleNoLock(fc, thread);
        }

        if(need_tick) {
            tick();
        }
    }

    /**
     * @brief 批量调度协程，如果加入前协程队列m_coroutines为空，则不管加入多少个coroutine，就只执行一次tick()函数，否则不执行
     * @param[in] begin 协程数组的开始
     * @param[in] end 协程数组的结束
     */
    template<class InputIterator>
    void coschedule(InputIterator begin, InputIterator end) {
        bool need_tick = false;
        {
            MtxType::Lock lock(m_mutex);
            while(begin != end) {
                need_tick = coscheduleNoLock(&*begin, -1) || need_tick;
                ++begin;
            }
        }
        if(need_tick) {
            tick();
        }
    }
    //将当前线程正在执行的协程切换到调用该函数的调度器thread线程中执行
    //当前线程的调度器和调用该函数的调度器不同也是可以的
    void switchTo(int thread = -1);
    std::ostream& dump(std::ostream& os);
protected:
    /**
     * @brief 通知协程调度器有任务了
     */

    virtual void tick();
    /**
     * @brief 协程调度函数
     */
    void run();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();

    /**
     * @brief 协程无任务可调度时执行idle协程
     */
    virtual void idle();

    /**
     * @brief 设置当前的协程调度器
     */
    void setThis();

    /**
     * @brief 是否有空闲线程
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0;}
private:
    /**
     * @brief 协程调度启动(无锁)
     */
    template<class CoroutineOrCb>
    bool coscheduleNoLock(CoroutineOrCb fc, int thread) {
        bool need_tick = m_coroutines.empty();
        CoroutineAndCppThread ft(fc, thread);
        if(ft.coroutine || ft.cb) {
            m_coroutines.push_back(ft);
        }
        return need_tick;
    }
private:
    /**
     * @brief 协程/函数/线程组
     */
    struct CoroutineAndCppThread {
        /// 协程
        Coroutine::ptr coroutine;
        /// 协程执行函数，最后要转换成协程对象
        std::function<void()> cb;
        /// 线程id
        int thread;

        /**
         * @brief 构造函数，增加了Coroutine的引用计数，原先的coroutine智能指针还能用
         * @param[in] f 协程
         * @param[in] thr 线程id
         */
        CoroutineAndCppThread(Coroutine::ptr f, int thr)
            :coroutine(f), thread(thr) {
        }

        /**
         * @brief 构造函数，不增加coroutine的引用计数，原先的coroutine智能指针不能用了
         * @param[in] f 协程指针
         * @param[in] thr 线程id
         * @post *f = nullptr
         */
        CoroutineAndCppThread(Coroutine::ptr* f, int thr)
            :thread(thr) {
            coroutine.swap(*f);
        }

        /**
         * @brief 构造函数，原先的function对象还能用
         * @param[in] f 协程执行函数
         * @param[in] thr 线程id
         */
        CoroutineAndCppThread(std::function<void()> f, int thr)
            :cb(f), thread(thr) {
        }

        /**
         * @brief 构造函数，原先的function对象不能用了
         * @param[in] f 协程执行函数指针
         * @param[in] thr 线程id
         * @post *f = nullptr
         */
        CoroutineAndCppThread(std::function<void()>* f, int thr)
            :thread(thr) {
            cb.swap(*f);
        }

        /**
         * @brief 无参构造函数
         */
        CoroutineAndCppThread()
            :thread(-1) {
        }

        /**
         * @brief 重置数据
         */
        void reset() {
            coroutine = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
private:
    /// Mtx：管理m_threads和m_coroutines的互斥量 
    MtxType m_mutex;
    /// 线程池，不包括主线程
    std::vector<Thread::ptr> m_threads;
    /// 待执行的协程队列
    std::list<CoroutineAndCppThread> m_coroutines;
    /// use_caller为true时有效, 创建的调度协程,用这个协程来进行其他协程的调度
    Coroutine::ptr m_rootCoroutine;
    /// 协程调度器名称
    std::string m_name;
protected:
    /// 协程下的线程id数组
    std::vector<int> m_threadIds;
    /// 线程数量
    size_t m_threadCount = 0;
    /// 工作线程数量
    std::atomic<size_t> m_activeCppThreadCount = {0};
    /// 空闲线程数量
    std::atomic<size_t> m_idleThreadCount = {0};
    /// 是否正在停止，在start函数中设置false，在stop()中设为true
    bool m_stopping = true;
    /// 是否自动停止，只在stop()中设为true
    bool m_autoStop = false;//
    /// 主线程id(use_caller)
    int m_rootThread = 0;
};

class CoSchedulerSwitcher : public Noncopyable {
public:
    CoSchedulerSwitcher(CoScheduler* target = nullptr);
    ~CoSchedulerSwitcher();
private:
    CoScheduler* m_caller;
};

}

#endif
