#ifndef __YHCHAOS_TIMED_COROUTINE_H__
#define __YHCHAOS_TIMER_COROUTINE_H__

#include <memory>
#include <vector>
#include <set>
#include "cpp_thread.h"

namespace yhchaos {

class TimedCoroutineManager;
/**
 * @brief 定时器
 */
class TimedCoroutine : public std::enable_shared_from_this<TimedCoroutine> {
friend class TimedCoroutineManager;
public:
    /// 定时器的智能指针类型
    typedef std::shared_ptr<TimedCoroutine> ptr;

    /**
     * @brief 取消定时器
     */
    bool cancel();

    /**
     * @brief 刷新设置定时器的执行时间
     */
    bool refresh();

    /**
     * @brief 重置定时器时间
     * @param[in] ms 定时器执行间隔时间(毫秒)
     * @param[in] from_now 是否从当前时间开始计算
     */
    bool reset(uint64_t ms, bool from_now);
private:
    /**
     * @brief 构造函数
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 回调函数
     * @param[in] recurring 是否循环
     * @param[in] manager 定时器管理器
     */
    TimedCoroutine(uint64_t ms, std::function<void()> cb,
          bool recurring, TimedCoroutineManager* manager);
    /**
     * @brief 构造函数
     * @param[in] next 执行的时间戳(毫秒)
     */
    TimedCoroutine(uint64_t next);
private:
    /// 是否循环定时器
    bool m_recurring = false;
    /// 执行周期
    uint64_t m_ms = 0;
    /// 精确的执行时间
    uint64_t m_next = 0;
    /// 回调函数
    std::function<void()> m_cb;
    /// 定时器管理器
    TimedCoroutineManager* m_manager = nullptr;
private:
    /**
     * @brief 定时器比较仿函数
     */
    struct Comparator {
        /**
         * @brief 比较定时器的智能指针的大小(按执行时间排序)
         * @param[in] lhs 定时器智能指针
         * @param[in] rhs 定时器智能指针
         */
        bool operator()(const TimedCoroutine::ptr& lhs, const TimedCoroutine::ptr& rhs) const;
    };
};

/**
 * @brief 定时器管理器
 */
class TimedCoroutineManager {
friend class TimedCoroutine;
public:
    /// 读写锁类型
    typedef RWMtx RWMtxType;

    /**
     * @brief 构造函数
     */
    TimedCoroutineManager();

    /**
     * @brief 析构函数
     */
    virtual ~TimedCoroutineManager();

    /**
     * @brief 添加定时器
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 定时器回调函数
     * @param[in] recurring 是否循环定时器\
     * @details 创建一个TimedCoroutine对象，并当添加到队列，如果添加到队头，并且是上次执行getNextTimedCoroutine()后首次添加计时器，那么触发onTimedCoroutineInsertedAtFront
     */
    TimedCoroutine::ptr addTimedCoroutine(uint64_t ms, std::function<void()> cb
                        ,bool recurring = false);

    /**
     * @brief 添加条件定时器
     * @param[in] ms 定时器执行间隔时间
     * @param[in] cb 定时器回调函数
     * @param[in] weak_cond 条件
     * @param[in] recurring 是否循环
     * @details 创建一个TimedCoroutine对象，将OnTimedCoroutine函数设置为TimedCoroutine的m_cb，OnTimedCoroutine接受weak_cond和cb，当weak_cond指向的资源还存在时，执行cb
     */
    TimedCoroutine::ptr addConditionTimedCoroutine(uint64_t ms, std::function<void()> cb
                        ,std::weak_ptr<void> weak_cond
                        ,bool recurring = false);

    /**
     * @brief 现在到最近一个（队头）定时器执行的时间间隔(毫秒)，并设置m_tickd为false，min(m_timers.begin()->m_next - now_ms, 0)
     */
    uint64_t getNextTimedCoroutine();

    /**
     * @brief 将已经到时的定时器函数放到cbs中返回，并将这些定时器从m_timers中删除
     * @param[out] cbs 回调函数数组
     */
    void listExpiredCb(std::vector<std::function<void()> >& cbs);

    /**
     * @brief 是否有定时器
     */
    bool hasTimedCoroutine();
protected:

    /**
     * @brief 当有新的定时器插入到定时器的首部,并且这次插入是上次执行getNextTimedCoroutine的首次插入，执行该函数
     */
    virtual void onTimedCoroutineInsertedAtFront() = 0;

    /**
     * @brief 将定时器添加到管理器中
     */
    void addTimedCoroutine(TimedCoroutine::ptr val, RWMtxType::WriteLock& lock);
private:
    /**
     * @brief 检测服务器时间是否被调后了
    */
    bool detectClockRollover(uint64_t now_ms);
private:
    /// Mtx
    RWMtxType m_mutex;
    /// 定时器集合
    std::set<TimedCoroutine::ptr, TimedCoroutine::Comparator> m_timers;
    /// 是否触发onTimedCoroutineInsertedAtFront
    bool m_tickd = false;
    /// 上次执行时间
    uint64_t m_previouseTime = 0;
};

}

#endif
