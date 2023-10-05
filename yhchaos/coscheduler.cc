#include "coscheduler.h"
#include "log.h"
#include "macro.h"
#include "hookfunc.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
static thread_local CoScheduler* t_coscheduler = nullptr;
static thread_local Coroutine* t_coscheduler_coroutine = nullptr;

CoScheduler::CoScheduler(size_t threads, bool use_caller, const std::string& name)//(1, true, "main")
    :m_name(name) {
    YHCHAOS_ASSERT(threads > 0);

    if(use_caller) {
        yhchaos::Coroutine::GetThis();
        --threads;

        YHCHAOS_ASSERT(GetThis() == nullptr);
        t_coscheduler = this;
        m_rootCoroutine.reset(new Coroutine(std::bind(&CoScheduler::run, this), 0, true));
        yhchaos::CppThread::SetName(m_name);
        //设置成主调度协程
        t_coscheduler_coroutine = m_rootCoroutine.get();
        m_rootCppThread = yhchaos::GetCppThreadId();
        m_threadIds.push_back(m_rootCppThread);
    } else {
        m_rootCppThread = -1;
    }
    m_threadCount = threads;
}

CoScheduler::~CoScheduler() {
    YHCHAOS_ASSERT(m_stopping);
    if(GetThis() == this) {
        t_coscheduler = nullptr;
    }
}

CoScheduler* CoScheduler::GetThis() {
    return t_coscheduler;
}

Coroutine* CoScheduler::GetMainCoroutine() {
    return t_coscheduler_coroutine;
}

void CoScheduler::start() {
    MtxType::Lock lock(m_mutex);
    if(!m_stopping) {
        return;
    }
    m_stopping = false;
    YHCHAOS_ASSERT(m_threads.empty());

    m_threads.resize(m_threadCount);
    for(size_t i = 0; i < m_threadCount; ++i) {
        //线程和调度协程执行同一个函数
        m_threads[i].reset(new CppThread(std::bind(&CoScheduler::run, this)
                            , m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    lock.unlock();

    //if(m_rootCoroutine) {
    //    //m_rootCoroutine->swapIn();
    //    m_rootCoroutine->call();
    //    YHCHAOS_LOG_INFO(g_logger) << "call out " << m_rootCoroutine->getState();
    //}
}

void CoScheduler::stop() {
    m_autoStop = true;
    //如果只设置了主线程，没有子线程，并且他的状态为TERM或者INIT，那么就直接返回
    if(m_rootCoroutine
            && m_threadCount == 0
            && (m_rootCoroutine->getState() == Coroutine::TERM
                || m_rootCoroutine->getState() == Coroutine::INIT)) {
        YHCHAOS_LOG_INFO(g_logger) << this << " stopped";
        m_stopping = true;

        if(stopping()) {
            return;
        }
    }

    //bool exit_on_this_coroutine = false;
    if(m_rootCppThread != -1) {
        YHCHAOS_ASSERT(GetThis() == this);
    } else {
        YHCHAOS_ASSERT(GetThis() != this);
    }

    m_stopping = true;
    //唤醒所有等待在epfd上的子线程，让其idle协程执行完毕处于TERM状态，进而结束所有子线程
    for(size_t i = 0; i < m_threadCount; ++i) {
        tick();
    }
    //唤醒rootCoroutine，并让其结束，转动主线程执行
    if(m_rootCoroutine) {
        //唤醒调度器创建线程
        tick();
    }
    if(m_rootCoroutine) {
        //while(!stopping()) {
        //    if(m_rootCoroutine->getState() == Coroutine::TERM
        //            || m_rootCoroutine->getState() == Coroutine::EXCEPT) {
        //        m_rootCoroutine.reset(new Coroutine(std::bind(&CoScheduler::run, this), 0, true));
        //        YHCHAOS_LOG_INFO(g_logger) << " root coroutine is term, reset";
        //        t_coroutine = m_rootCoroutine.get();
        //    }
        //    m_rootCoroutine->call();
        //}
        //此时m_coroutines中还有在等待的协程或者还有正在执行coroutine的线程，切换到调度协程，
        //通过调度协程来调度\程池中的线程去执行完正在等待的coroutine任务,当所有的coroutine任务执行完毕之后，
        if(!stopping()) {
            m_rootCoroutine->call();
        }
    }

    std::vector<CppThread::ptr> thrs;
    {
        MtxType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }

    for(auto& i : thrs) {
        i->join();
    }
    //if(exit_on_this_coroutine) {
    //}
}

void CoScheduler::setThis() {
    t_coscheduler = this;
}
//协程调度器的主要执行函数，用于协程的调度、切换和执行。
void CoScheduler::run() {
    YHCHAOS_LOG_DEBUG(g_logger) << m_name << " run";
    //所有的系统调用都是hook版本的
    set_hook_enable(true);
    setThis();
    if(yhchaos::GetCppThreadId() != m_rootCppThread) {
        t_coscheduler_coroutine = Coroutine::GetThis().get();
    }
    Coroutine::ptr idle_coroutine(new Coroutine(std::bind(&CoScheduler::idle, this)));
    Coroutine::ptr cb_coroutine;

    CoroutineAndCppThread ft;
    while(true) {
        ft.reset();
        bool tick_me = false;
        bool is_active = false;
        {
            MtxType::Lock lock(m_mutex);
            auto it = m_coroutines.begin();
            while(it != m_coroutines.end()) {
                if(it->thread != -1 && it->thread != yhchaos::GetCppThreadId()) {
                    ++it;
                    //该coroutine不应该由此线程执行，唤醒另一个线程读取该coroutine
                    tick_me = true;
                    continue;
                }

                YHCHAOS_ASSERT(it->coroutine || it->cb);
                //如果是应该由该线程处理的协程，那么该协程不应该处于执行状态
                if(it->coroutine && it->coroutine->getState() == Coroutine::EXEC) {
                    ++it;
                    continue;
                }
                //找到了该线程可以处理的协程,将其从m_coroutines中移除,活跃状态的线程数量加1
                ft = *it;
                m_coroutines.erase(it++);
                ++m_activeCppThreadCount;
                is_active = true;
                break;
            }
            //tick只在只有在当前线程没有找到自己应该处理的coroutine，并且m_coroutine队列中由其他coroutine的时候，
            //设置为true，表示队列中由协程等待其他线程来处理，需要唤醒一个
            tick_me |= it != m_coroutines.end();
        }
        if(tick_me) {
            tick();
        }
        //TERM表示coroutine函数正确执行完毕，EXCEPT表示coroutine函数执行出错，在执行过程抛出异常
        //处于INIT | READY | HOLD | EXEC状态的协程都是可以被调度的，
        //处于TERM | EXCEPT状态的协程都是不可以被调度的
        if(ft.coroutine && (ft.coroutine->getState() != Coroutine::TERM
                        && ft.coroutine->getState() != Coroutine::EXCEPT)) {
            ft.coroutine->swapIn();
            --m_activeCppThreadCount;
            if(ft.coroutine->getState() == Coroutine::READY) {
                coschedule(ft.coroutine);
            } else if(ft.coroutine->getState() != Coroutine::TERM
                    && ft.coroutine->getState() != Coroutine::EXCEPT) {
                ft.coroutine->m_state = Coroutine::HOLD;
            }
            ft.reset();
            //如果m_coroutines中存的是函数，那么就为这个函数创建一个协程对象
        } else if(ft.cb) {
            if(cb_coroutine) {
                cb_coroutine->reset(ft.cb);
            } else {
                cb_coroutine.reset(new Coroutine(ft.cb));
            }
            ft.reset();
            cb_coroutine->swapIn();
            --m_activeCppThreadCount;
            if(cb_coroutine->getState() == Coroutine::READY) {
                //增加cb_coroutine的引用计数，将其放入m_coroutines中
                coschedule(cb_coroutine);
                cb_coroutine.reset()
            } else if(cb_coroutine->getState() == Coroutine::EXCEPT
                    || cb_coroutine->getState() == Coroutine::TERM) {
                cb_coroutine->reset(nullptr);
            } else {
                cb_coroutine->m_state = Coroutine::HOLD;
                cb_coroutine.reset();
            }
        } else {
            //当前线程没有找到自己可以处理的协程对象（任务），那么就转到空闲协程去执行
            if(is_active) {
                --m_activeCppThreadCount;
                continue;
            }
            if(idle_coroutine->getState() == Coroutine::TERM) {
                YHCHAOS_LOG_INFO(g_logger) << "idle coroutine term";
                break;
            }

            ++m_idleCppThreadCount;
            idle_coroutine->swapIn();
            --m_idleCppThreadCount;
            if(idle_coroutine->getState() != Coroutine::TERM
                    && idle_coroutine->getState() != Coroutine::EXCEPT) {
                idle_coroutine->m_state = Coroutine::HOLD;
            }
        }
    }
}

void CoScheduler::tick() {
    YHCHAOS_LOG_INFO(g_logger) << "tick";
}
//执行完stop函数，且没有线程正在执行coroutine，且没有正在等待执行的coroutine，返回true
bool CoScheduler::stopping() {
    MtxType::Lock lock(m_mutex);
    //线程池里的线程刚启动时，m_autoStop=false, m_stopping=false
    return m_autoStop && m_stopping
        && m_coroutines.empty() && m_activeCppThreadCount == 0; 
}

void CoScheduler::idle() {
    YHCHAOS_LOG_INFO(g_logger) << "idle";
    while(!stopping()) {
        yhchaos::Coroutine::YieldToHold();
    }
}

void CoScheduler::switchTo(int thread) {
    YHCHAOS_ASSERT(CoScheduler::GetThis() != nullptr);
    if(CoScheduler::GetThis() == this) {
        if(thread == -1 || thread == yhchaos::GetCppThreadId()) {
            return;
        }
    }
    coschedule(Coroutine::GetThis(), thread);
    Coroutine::YieldToHold();
}

std::ostream& CoScheduler::dump(std::ostream& os) {
    os << "[CoScheduler name=" << m_name
       << " size=" << m_threadCount
       << " active_count=" << m_activeCppThreadCount
       << " idle_count=" << m_idleCppThreadCount
       << " stopping=" << m_stopping
       << " ]" << std::endl << "    ";
    for(size_t i = 0; i < m_threadIds.size(); ++i) {
        if(i) {
            os << ", ";
        }
        os << m_threadIds[i];
    }
    return os;
}

CoSchedulerSwitcher::CoSchedulerSwitcher(CoScheduler* target) {
    m_caller = CoScheduler::GetThis();
    if(target) {
        target->switchTo();
    }
}

CoSchedulerSwitcher::~CoSchedulerSwitcher() {
    if(m_caller) {
        m_caller->switchTo();
    }
}

}
