#include "timed_coroutine.h"
#include "util.h"

namespace yhchaos {

bool TimedCoroutine::Comparator::operator()(const TimedCoroutine::ptr& lhs
                        ,const TimedCoroutine::ptr& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    if(!rhs) {
        return false;
    }
    if(lhs->m_next < rhs->m_next) {
        return true;
    }
    if(rhs->m_next < lhs->m_next) {
        return false;
    }
    return lhs.get() < rhs.get();
}


TimedCoroutine::TimedCoroutine(uint64_t ms, std::function<void()> cb,
             bool recurring, TimedCoroutineManager* manager)
    :m_recurring(recurring)
    ,m_ms(ms)
    ,m_cb(cb)
    ,m_manager(manager) {
    m_next = yhchaos::GetCurrentMS() + m_ms;
}

TimedCoroutine::TimedCoroutine(uint64_t next)
    :m_next(next) {
}

bool TimedCoroutine::cancel() {
    TimedCoroutineManager::RWMtxType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

bool TimedCoroutine::refresh() {
    TimedCoroutineManager::RWMtxType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    m_next = yhchaos::GetCurrentMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool TimedCoroutine::reset(uint64_t ms, bool from_now) {
    if(ms == m_ms && !from_now) {
        return true;
    }
    TimedCoroutineManager::RWMtxType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if(from_now) {
        start = yhchaos::GetCurrentMS();
    } else {
        start = m_next - m_ms;
    }
    m_ms = ms;
    m_next = start + m_ms;
    m_manager->addTimedCoroutine(shared_from_this(), lock);
    return true;

}

TimedCoroutineManager::TimedCoroutineManager() {
    m_previouseTime = yhchaos::GetCurrentMS();
}

TimedCoroutineManager::~TimedCoroutineManager() {
}

TimedCoroutine::ptr TimedCoroutineManager::addTimedCoroutine(uint64_t ms, std::function<void()> cb
                                  ,bool recurring) {
    TimedCoroutine::ptr timer(new TimedCoroutine(ms, cb, recurring, this));//引用计数1
    RWMtxType::WriteLock lock(m_mutex);
    addTimedCoroutine(timer, lock);//引用计数2
    return timer;
}
//到时间并且weak_cond还存在的时候，执行cb
static void OnTimedCoroutine(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) {
        cb();
    }
}

TimedCoroutine::ptr TimedCoroutineManager::addConditionTimedCoroutine(uint64_t ms, std::function<void()> cb
                                    ,std::weak_ptr<void> weak_cond
                                    ,bool recurring) {
    return addTimedCoroutine(ms, std::bind(&OnTimedCoroutine, weak_cond, cb), recurring);
}

uint64_t TimedCoroutineManager::getNextTimedCoroutine() {
    RWMtxType::ReadLock lock(m_mutex);
    m_tickd = false;
    if(m_timers.empty()) {
        return ~0ull;
    }

    const TimedCoroutine::ptr& next = *m_timers.begin();
    uint64_t now_ms = yhchaos::GetCurrentMS();
    if(now_ms >= next->m_next) {
        return 0;
    } else {
        return next->m_next - now_ms;
    }
}

void TimedCoroutineManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    uint64_t now_ms = yhchaos::GetCurrentMS();
    std::vector<TimedCoroutine::ptr> expired;
    {
        RWMtxType::ReadLock lock(m_mutex);
        if(m_timers.empty()) {
            return;
        }
    }
    RWMtxType::WriteLock lock(m_mutex);
    if(m_timers.empty()) {
        return;
    }
    bool rollover = detectClockRollover(now_ms);
    if(!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return;
    }

    TimedCoroutine::ptr now_timer(new TimedCoroutine(now_ms));
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    while(it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    for(auto& timer : expired) {
        cbs.push_back(timer->m_cb);
        if(timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
    //expired会被删除，所以计时队列的引用计数会-1，cb会被设为nullptr
}

void TimedCoroutineManager::addTimedCoroutine(TimedCoroutine::ptr val, RWMtxType::WriteLock& lock) {//val:引用计数2
    auto it = m_timers.insert(val).first;//引用计数3
    bool at_front = (it == m_timers.begin()) && !m_tickd;
    if(at_front) {
        m_tickd = true;
    }
    lock.unlock();

    if(at_front) {
        onTimedCoroutineInsertedAtFront();
    }
    //函数结束：引用计数3-1=2
}

bool TimedCoroutineManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    if(now_ms < m_previouseTime &&
            now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

bool TimedCoroutineManager::hasTimedCoroutine() {
    RWMtxType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}
