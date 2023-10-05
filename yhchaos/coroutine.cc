#include "coroutine.h"
#include "appconfig.h"
#include "macro.h"
#include "log.h"
#include "coscheduler.h"
#include <atomic>

namespace yhchaos {

// 用于记录日志的 Logger 对象
static Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
// s_coroutine_id: 静态原子变量，用于分配协程的唯一ID。
static std::atomic<uint64_t> s_coroutine_id {0};
// s_coroutine_count: 静态原子变量，用于记录当前存在的协程数量。
static std::atomic<uint64_t> s_coroutine_count {0};

//记录线程中第一个协程对象的地址
//记录线程中第一个协程对象的地址
//t_coroutine 是程局部变量,用于存储当前线程正在运行的协程对象。这是用于实际执行协程逻辑的协程。t_coroutine 的作用是跟踪当前线程正在执行的协程，
//以便在协程切换和调度过程中能够正确地切换到正在运行的协程。多个函数中设置了 t_coroutine，主要用于在协程切换时设置当前线程的运行协程。
static thread_local Coroutine* t_coroutine = nullptr;
//t_threadCoroutine 是一个线程局部变量，用于存储当前线程中的主协程对象。主协程是线程上运行的主协程，
//他可以开启一个新的协程（子协程），要创建协程必须要回到main_coroutine中去，子协程退出的时候，
//要把执行控制权还回main_coroutine中去，然后main_coroutine再去协调其他的协程执行
//线程中的协程都是有主协程分配，并且又由主协程进行切换回收的

static thread_local Coroutine::ptr t_threadCoroutine = nullptr;

// 协程栈大小的配置变量。
static AppConfigVar<uint32_t>::ptr g_coroutine_stack_size =
    AppConfig::SearchFor<uint32_t>("coroutine.stack_size", 128 * 1024, "coroutine stack size");

class MallocStackAllocator {
public:
    static void* Alloc(size_t size) {
        return malloc(size);
    }

    static void Dealloc(void* vp, size_t size) {
        return free(vp);
    }
};

using StackAllocator = MallocStackAllocator;

uint64_t Coroutine::GetCoroutineId() {
    if(t_coroutine) {
        return t_coroutine->getId();
    }
    return 0;
}

Coroutine::Coroutine() {
    m_state = EXEC;  
    SetThis(this);   

    if(getcontext(&m_ctx)) {  
        YHCHAOS_ASSERT2(false, "getcontext");  
    }

    ++s_coroutine_count; 

    YHCHAOS_LOG_DEBUG(g_logger) << "Coroutine::Coroutine main";  
}


Coroutine::Coroutine(std::function<void()> cb, size_t stacksize, bool use_caller)
    :m_id(++s_coroutine_id)
    ,m_cb(cb) {
    ++s_coroutine_count;
    m_stacksize = stacksize ? stacksize : g_coroutine_stack_size->getValue();

    m_stack = StackAllocator::Alloc(m_stacksize);
    if(getcontext(&m_ctx)) {
        YHCHAOS_ASSERT2(false, "getcontext");
    }
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    if(!use_caller) {
        makecontext(&m_ctx, &Coroutine::MainFunc, 0);
    } else {
        makecontext(&m_ctx, &Coroutine::CallerMainFunc, 0);
    }
    YHCHAOS_LOG_DEBUG(g_logger) << "Coroutine::Coroutine id=" << m_id;
}
Coroutine::~Coroutine() {
    --s_coroutine_count;
    if(m_stack) {
        YHCHAOS_ASSERT(m_state == TERM
                || m_state == EXCEPT
                || m_state == INIT);

        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else {
        //确认是不是主协程，主协程没有m_cb函数，主协程的执行状态是EXEC
        YHCHAOS_ASSERT(!m_cb);
        YHCHAOS_ASSERT(m_state == EXEC);

        Coroutine* cur = t_coroutine;
        if(cur == this) {
            SetThis(nullptr);
        }
    }
    YHCHAOS_LOG_DEBUG(g_logger) << "Coroutine::~Coroutine id=" << m_id
                              << " total=" << s_coroutine_count;
}

//重置协程函数，并重置状态，一个协程已经执行完了，但是分配的内存没释放，基于这个内存来继续创建一个新的协程
//将其重新初始化
//INIT，TERM, EXCEPT
void Coroutine::reset(std::function<void()> cb) {
    YHCHAOS_ASSERT(m_stack);
    YHCHAOS_ASSERT(m_state == TERM
            || m_state == EXCEPT
            || m_state == INIT);
    m_cb = cb;
    if(getcontext(&m_ctx)) {
        YHCHAOS_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Coroutine::MainFunc, 0);
    m_state = INIT;
}
//将当前线程切换到执行当前协程的状态，即将控制权从当前线程切换到当前协程上，使得当前协程可以继续执行。
//与 swapIn() 函数不同，call() 函数会直接将当前线程切换到协程执行，而不会切换到后台。
void Coroutine::call() {
    SetThis(this);
    m_state = EXEC;
    if(swapcontext(&t_threadCoroutine->m_ctx, &m_ctx)) {
        YHCHAOS_ASSERT2(false, "swapcontext");
    }
}
//从子协程转移会主协程（=主线程）最后一次swapIn调用的下一条语句，
void Coroutine::back() {
    SetThis(t_threadCoroutine.get());
    if(swapcontext(&m_ctx, &t_threadCoroutine->m_ctx)) {
        YHCHAOS_ASSERT2(false, "swapcontext");
    }
}
//作用：实现协程的切换，将控制权从当前运行的协程切换到另一个协程，把他们交换一下，把当前正在运行的协程切换到后台，
void Coroutine::swapIn() {
    SetThis(this);
    YHCHAOS_ASSERT(m_state != EXEC);
    m_state = EXEC;
    if(swapcontext(&CoScheduler::GetMainCoroutine()->m_ctx, &m_ctx)) {
        YHCHAOS_ASSERT2(false, "swapcontext");
    }
}

void Coroutine::swapOut() {
    SetThis(CoScheduler::GetMainCoroutine());
    if(swapcontext(&m_ctx, &CoScheduler::GetMainCoroutine()->m_ctx)) {
        YHCHAOS_ASSERT2(false, "swapcontext");
    }
}

void Coroutine::SetThis(Coroutine* f) {
    t_coroutine = f;
}

Coroutine::ptr Coroutine::GetThis() {
    if(t_coroutine) {
        return t_coroutine->shared_from_this();
    }
    //如果没有设置t_coroutine,则表示没有创建主协程，那么就创建一个主协程
    Coroutine::ptr main_coroutine(new Coroutine);
    YHCHAOS_ASSERT(t_coroutine == main_coroutine.get());
    t_threadCoroutine = main_coroutine;
    //如果没有主协程，就创建一个并返回主协程对象
    return t_coroutine->shared_from_this();
}

void Coroutine::YieldToReady() {
    Coroutine::ptr cur = GetThis();
    YHCHAOS_ASSERT(cur->m_state == EXEC);
    cur->m_state = READY;
    cur->swapOut();
}

//协程切换到后台，并且设置为Hold状态
void Coroutine::YieldToHold() {
    Coroutine::ptr cur = GetThis();
    YHCHAOS_ASSERT(cur->m_state == EXEC);
    //cur->m_state = HOLD;
    cur->swapOut();
}

//总协程数
uint64_t Coroutine::TotalCoroutines() {
    return s_coroutine_count;
}
//用于执行协程的实际逻辑。这个函数会在协程切换到执行状态后被调用，执行协程的用户定义的函数。
void Coroutine::MainFunc() {
    Coroutine::ptr cur = GetThis();
    YHCHAOS_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        YHCHAOS_LOG_ERROR(g_logger) << "Coroutine Except: " << ex.what()
            << " coroutine_id=" << cur->getId()
            << std::endl
            << yhchaos::BacktraceToString();
    } catch (...) {
        cur->m_state = EXCEPT;
        YHCHAOS_LOG_ERROR(g_logger) << "Coroutine Except"
            << " coroutine_id=" << cur->getId()
            << std::endl
            << yhchaos::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->swapOut();

    YHCHAOS_ASSERT2(false, "never reach coroutine_id=" + std::to_string(raw_ptr->getId()));
}
//通过swapIn等会将控制权转移到这个函数中，也就是说这个函数是在当前新协程中执行的
void Coroutine::CallerMainFunc() {
    Coroutine::ptr cur = GetThis();
    YHCHAOS_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        YHCHAOS_LOG_ERROR(g_logger) << "Coroutine Except: " << ex.what()
            << " coroutine_id=" << cur->getId()
            << std::endl
            << yhchaos::BacktraceToString();
    } catch (...) {
        cur->m_state = EXCEPT;
        YHCHAOS_LOG_ERROR(g_logger) << "Coroutine Except"
            << " coroutine_id=" << cur->getId()
            << std::endl
            << yhchaos::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    //这个是存储当前协程对象的智能指针的reset，如果不调用reset那么这个协程对象就不会释放，因为
    //上面Coroutine::ptr cur = GetThis();使得这个协程对象多了一个引用计数
    cur.reset();
    raw_ptr->back();
    YHCHAOS_ASSERT2(false, "never reach coroutine_id=" + std::to_string(raw_ptr->getId()));

}

}
