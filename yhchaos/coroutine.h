#ifndef __YHCHAOS_COROUTINE_H__
#define __YHCHAOS_COROUTINE_H__

#include <memory>
#include <functional>
#include <ucontext.h>

namespace yhchaos {

class CoScheduler;

/**
 * @brief 协程类
 */
class Coroutine : public std::enable_shared_from_this<Coroutine> {
friend class CoScheduler;
public:
    typedef std::shared_ptr<Coroutine> ptr;

    /**
     * @brief 协程状态
     * @details 状态转换
     *  INIT(构造函数/reset(m_cb)) -> EXEC(swapIn/call) 
     * -> HOLD(YieldToHold) | TERM(正常执行完协程函数) |  EXCEPT(协程函数执行过程中出现异常) | READY(YieldToReady)
     */
    enum State {
        INIT,
        HOLD,
        EXEC,
        TERM,
        READY,
        EXCEPT
    };
private:
    /**
     * @brief 无参构造函数
     * @attention 每个线程第一个协程的构造
     */
    Coroutine();

public:
    /**
     * @brief 构造函数
     * @param[in] cb 协程执行的函数
     * @param[in] stacksize 协程栈大小
     * @param[in] use_caller 是否在MainCoroutine上调度
     */
    Coroutine(std::function<void()> cb, size_t stacksize = 0, bool use_caller = false);

    /**
     * @brief 析构函数
     */
    ~Coroutine();

    /**
     * @brief 重置协程执行函数,并设置状态，通常在协程初始化、结束或异常时使用。
     * @pre getState() 为 INIT, TERM, EXCEPT
     * @post getState() = INIT
     */
    void reset(std::function<void()> cb);

    /**
     * @brief 将当前协程切换到运行状态
     * @pre getState() != EXEC
     * @post getState() = EXEC
     */

    void swapIn();

    /**
     * @brief  当前协程不执行了，将执行权让出来，将当前协程切换到后台，暂停执行。
     */
    void swapOut();

    /**
     * @brief 将当前线程切换到执行状态，通常用于线程启动时切换到主协程执行。
     * @pre 执行的为当前线程的主协程
     */
    void call();

    /**
     * @brief 将当前线程切换到后台，用于协程结束或暂停时切换回线程的主协程。
     * @pre 执行的为该协程
     * @post 返回到线程的主协程
     */
    void back();

    /**
     * @brief 返回协程id
     */
    uint64_t getId() const { return m_id;}

    /**
     * @brief 返回协程状态
     */
    State getState() const { return m_state;}
public:

    /**
     * @brief 设置当前线程的运行协程
     * @param[in] f 运行协程
     */
    static void SetThis(Coroutine* f);

    /**
     * @brief 返回当前所在的协程
     */
    static Coroutine::ptr GetThis();

    /**
     * @brief 让出执行权并将自己的状态设置成ready，将当前协程切换到后台,并设置为READY状态
     * @post getState() = READY
     */
    static void YieldToReady();

    /**
     * @brief 让出执行权病设置一下自己的状态为Hold，将当前协程切换到后台,并设置为HOLD状态
     * @post getState() = HOLD
     */
    static void YieldToHold();

    /**
     * @brief 返回当前协程的总数量
     */
    static uint64_t TotalCoroutines();

    /**
     * @brief 协程执行函数，用于执行协程的实际逻辑。
     * @post 执行完成返回到线程主协程
     */
    static void MainFunc();

    /**
     * @brief 协程执行函数，用于执行协程在使用了调度器的情况下的实际逻辑。
     * @post 执行完成返回到线程调度协程
     */
    static void CallerMainFunc();

    /**
     * @brief 获取当前协程的id
     */
    static uint64_t GetCoroutineId();
private:
    /// 协程id
    uint64_t m_id = 0;
    /// 协程运行栈大小
    uint32_t m_stacksize = 0;
    /// 协程状态
    State m_state = INIT;
    /// 协程上下文
    ucontext_t m_ctx;
    /// 协程运行栈指针
    void* m_stack = nullptr;
    /// 协程运行函数
    std::function<void()> m_cb;
};

}

#endif
