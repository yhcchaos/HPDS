#ifndef __YHCHAOS_MTX_H__
#define __YHCHAOS_MTX_H__

#include <thread>
#include <functional>
#include <memory>
#include <cpp_thread.h>
#include <semaphore.h>
#include <stdint.h>
#include <atomic>
#include <list>

#include "noncopyable.h"
#include "coroutine.h"

namespace yhchaos {

/**
 * @brief 信号量
 */
class Sem : Noncopyable {
public:
    /**
     * @brief 构造函数
     * @param[in] count 信号量值的大小
     */ 
    Sem(uint32_t count = 0);

    /**
     * @brief 析构函数
     */
    ~Sem();

    /**
     * @brief 获取信号量
     */
    void wait();

    /**
     * @brief 释放信号量
     */
    void notify();
private:
    sem_t m_semaphore;
};

/**
 * @brief 局部锁的模板实现,不阻塞
 */
template<class T>
struct ScopLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex Mtx
     */
    ScopLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ScopLockImpl() {
        unlock();
    }

    /**
     * @brief 加锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.lock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部读锁模板实现，非阻塞
 */
template<class T>
struct ReadScopLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    ReadScopLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.rdlock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ReadScopLockImpl() {
        unlock();
    }

    /**
     * @brief 上读锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    /**
     * @brief 释放锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部写锁模板实现
 */
template<class T>
struct WriteScopLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    WriteScopLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.wrlock();
        m_locked = true;
    }

    /**
     * @brief 析构函数
     */
    ~WriteScopLockImpl() {
        unlock();
    }

    /**
     * @brief 上写锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// Mtx
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 互斥量，阻塞
 */
class Mtx : Noncopyable {
public: 
    /// 局部锁
    typedef ScopLockImpl<Mtx> Lock;

    /**
     * @brief 构造函数
     */
    Mtx() {
        pthread_mutex_init(&m_mutex, nullptr);
    }

    /**
     * @brief 析构函数
     */
    ~Mtx() {
        pthread_mutex_destroy(&m_mutex);
    }

    /**
     * @brief 加锁
     */
    void lock() {
        pthread_mutex_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_mutex_unlock(&m_mutex);
    }
private:
    /// mutex
    pthread_mutex_t m_mutex;
};

/**
 * @brief 空锁(用于调试)
 */
class NullMtx : Noncopyable{
public:
    /// 局部锁
    typedef ScopLockImpl<NullMtx> Lock;

    /**
     * @brief 构造函数
     */
    NullMtx() {}

    /**
     * @brief 析构函数
     */
    ~NullMtx() {}

    /**
     * @brief 加锁
     */
    void lock() {}

    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 读写互斥量，局部读锁和局部写锁用同一个RWMtx来构造就行了，传递引用
 */
class RWMtx : Noncopyable{
public:

    /// 局部读锁
    typedef ReadScopLockImpl<RWMtx> ReadLock;

    /// 局部写锁
    typedef WriteScopLockImpl<RWMtx> WriteLock;

    /**
     * @brief 构造函数
     */
    RWMtx() {
        pthread_rwlock_init(&m_lock, nullptr);
    }
    
    /**
     * @brief 析构函数
     */
    ~RWMtx() {
        pthread_rwlock_destroy(&m_lock);
    }

    /**
     * @brief 上读锁
     */
    void rdlock() {
        pthread_rwlock_rdlock(&m_lock);
    }

    /**
     * @brief 上写锁
     */
    void wrlock() {
        pthread_rwlock_wrlock(&m_lock);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_rwlock_unlock(&m_lock);
    }
private:
    /// 读写锁
    pthread_rwlock_t m_lock;
};

/**
 * @brief 空读写锁(用于调试)
 */
class NullRWMtx : Noncopyable {
public:
    /// 局部读锁
    typedef ReadScopLockImpl<NullMtx> ReadLock;
    /// 局部写锁
    typedef WriteScopLockImpl<NullMtx> WriteLock;

    /**
     * @brief 构造函数
     */
    NullRWMtx() {}
    /**
     * @brief 析构函数
     */
    ~NullRWMtx() {}

    /**
     * @brief 上读锁
     */
    void rdlock() {}

    /**
     * @brief 上写锁
     */
    void wrlock() {}
    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 自旋锁
 */
class Splock : Noncopyable {
public:
    /// 局部锁
    typedef ScopLockImpl<Splock> Lock;

    /**
     * @brief 构造函数
     */
    Splock() {
        pthread_spin_init(&m_mutex, 0);
    }

    /**
     * @brief 析构函数
     */
    ~Splock() {
        pthread_spin_destroy(&m_mutex);
    }

    /**
     * @brief 上锁
     */
    void lock() {
        pthread_spin_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_spin_unlock(&m_mutex);
    }
private:
    /// 自旋锁
    pthread_spinlock_t m_mutex;
};

/**
 * @brief 原子锁
 */
class CASLock : Noncopyable {
public:
    /// 局部锁
    typedef ScopLockImpl<CASLock> Lock;

    /**
     * @brief 构造函数
     */
    CASLock() {
        m_mutex.clear();
    }

    /**
     * @brief 析构函数
     */
    ~CASLock() {
    }

    /**
     * @brief 上锁
     */
    void lock() {
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }
private:
    /// 原子状态
    volatile std::atomic_flag m_mutex;
};

//协程
class CoScheduler;
class CoroutineSem : Noncopyable {
public:
    typedef Splock MtxType;

    CoroutineSem(size_t initial_concurrency = 0);
    ~CoroutineSem();

    bool tryWait();
    //m_concurrency>0的时候才会继续执行当前的coroutine，然后将m_concurrency-1;否则就会将当前的coroutine加入到m_waiters中
    //然后将当前调度器的当前协程挂起，返回主协程,不减小m_concurrency，也就是说当m_concurrency最小值为0
    void wait();
    //如果m_waiters不为空，就会将m_waiters中的第一个协程唤醒，加入对应调度器中进行调度，此时m_concurrency=0
    //不增加m_concurrency。
    //否则只是递增m_concurrency
    void notify();

    size_t getConcurrency() const { return m_concurrency;}
    void reset() { m_concurrency = 0;}
private:
    MtxType m_mutex;
    std::list<std::pair<CoScheduler*, Coroutine::ptr> > m_waiters;
    size_t m_concurrency;
};



}

#endif
