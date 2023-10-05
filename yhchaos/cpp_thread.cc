#include "cpp_thread.h"
#include "log.h"
#include "util.h"

namespace yhchaos {
//当前线程的线程句柄
static thread_local CppThread* t_thread = nullptr;
//当前线程的线程名称
static thread_local std::string t_thread_name = "UNKNOW";

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

CppThread* CppThread::GetThis() {
    return t_thread;
}

const std::string& CppThread::GetName() {
    return t_thread_name;
}

void CppThread::SetName(const std::string& name) {
    if(name.empty()) {
        return;
    }
    if(t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

CppThread::CppThread(std::function<void()> cb, const std::string& name)
    :m_cb(cb)
    ,m_name(name) {
    if(name.empty()) {
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &CppThread::run, this);
    if(rt) {
        YHCHAOS_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
            << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

CppThread::~CppThread() {
    if(m_thread) {
        pthread_detach(m_thread);
    }
}

void CppThread::join() {
    if(m_thread) {
        int rt = pthread_join(m_thread, nullptr);
        if(rt) {
            YHCHAOS_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
                << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* CppThread::run(void* arg) {
    CppThread* thread = (CppThread*)arg;
    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = yhchaos::GetCppThreadId();
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    //thread对象中的cb为nullptr了
    cb.swap(thread->m_cb);

    thread->m_semaphore.notify();

    cb();
    return 0;
}

}
