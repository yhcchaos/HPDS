#include "watch_cpp_thread.h"
#include "yhchaos/appconfig.h"
#include "yhchaos/log.h"
#include "yhchaos/util.h"
#include "yhchaos/macro.h"
#include "yhchaos/appconfig.h"
#include <iomanip>

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

static yhchaos::AppConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_thread_info_set
            = AppConfig::SearchFor("fox_thread", std::map<std::string, std::map<std::string, std::string> >()
                    ,"confg for thread");
//保护s_thread_names
static RWMtx s_thread_mutex;
//thread_id:name
static std::map<uint64_t, std::string> s_thread_names;

thread_local WatchCppThread* s_thread = nullptr;

void WatchCppThread::read_cb(evutil_socket_t sock, short which, void* args) {
    WatchCppThread* thread = static_cast<WatchCppThread*>(args);
    uint8_t cmd[4096];
    if(recv(sock, cmd, sizeof(cmd), 0) > 0) {
        std::list<callback> callbacks;
        RWMtx::WriteLock lock(thread->m_mutex);
        callbacks.swap(thread->m_callbacks);
        lock.unlock();
        thread->m_working = true;
        for(auto it = callbacks.begin();
                it != callbacks.end(); ++it) {
            if(*it) {
                //YHCHAOS_ASSERT(thread == GetThis());
                try {
                    (*it)();
                } catch (std::exception& ex) {
                    YHCHAOS_LOG_ERROR(g_logger) << "exception:" << ex.what();
                } catch (const char* c) {
                    YHCHAOS_LOG_ERROR(g_logger) << "exception:" << c;
                } catch (...) {
                    YHCHAOS_LOG_ERROR(g_logger) << "uncatch exception";
                }
            } else {
                event_base_loopbreak(thread->m_base);
                thread->m_start = false;
                thread->unsetThis();
                break;
            }
        }
        yhchaos::Atomic::addFetch(thread->m_total, callbacks.size());
        thread->m_working = false;
    }
}

WatchCppThread::WatchCppThread(const std::string& name, struct event_base* base)
    :m_read(0)
    ,m_write(0)
    ,m_base(NULL)
    ,m_event(NULL)
    ,m_thread(NULL)
    ,m_name(name)
    ,m_working(false)
    ,m_start(false)
    ,m_total(0) {
    int fds[2];
    if(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        //YHCHAOS_LOG_ERROR(g_logger) << "WatchCppThread init error";
        throw std::logic_error("thread init error");
    }

    evutil_make_socket_nonblocking(fds[0]);
    evutil_make_socket_nonblocking(fds[1]);

    m_read = fds[0];
    m_write = fds[1];

    if(base) {
        m_base = base;
        setThis();
    } else {
        m_base = event_base_new();
    }
    m_event = event_new(m_base, m_read, EV_READ | EV_PERSIST, read_cb, this);
    event_add(m_event, NULL);
}

void WatchCppThread::dump(std::ostream& os) {
    RWMtx::ReadLock lock(m_mutex);
    os << "[thread name=" << m_name
       << " working=" << m_working
       << " tasks=" << m_callbacks.size()
       << " total=" << m_total
       << "]" << std::endl;
}

std::thread::id WatchCppThread::getId() const {
    if(m_thread) {
        return m_thread->get_id();
    }
    return std::thread::id();
}

void* WatchCppThread::getData(const std::string& name) {
    //Mtx::ReadLock lock(m_mutex);
    auto it = m_datas.find(name);
    return it == m_datas.end() ? nullptr : it->second;
}

void  WatchCppThread::setData(const std::string& name, void* v) {
    //Mtx::WriteLock lock(m_mutex);
    m_datas[name] = v;
}

WatchCppThread::~WatchCppThread() {
    if(m_read) {
        close(m_read);
    }
    if(m_write) {
        close(m_write);
    }
    stop();
    join();
    if(m_thread) {
        delete m_thread;
    }
    if(m_event) {
        event_free(m_event);
    }
    if(m_base) {
        event_base_free(m_base);
    }
}

void WatchCppThread::start() {
    if(m_thread) {
        //YHCHAOS_LOG_ERROR(g_logger) << "WatchCppThread is running";
        throw std::logic_error("WatchCppThread is running");
    }

    m_thread = new std::thread(std::bind(&WatchCppThread::thread_cb, this));
    m_start = true;
}

void WatchCppThread::thread_cb() {
    //std::cout << "WatchCppThread(" << m_name << "," << pthread_self() << ")" << std::endl;
    setThis();
    pthread_setname_np(pthread_self(), m_name.substr(0, 15).c_str());
    if(m_initCb) {
        m_initCb(this);
        m_initCb = nullptr;
    }
    event_base_loop(m_base, 0);
}

bool WatchCppThread::dispatch(callback cb) {
    RWMtx::WriteLock lock(m_mutex);
    m_callbacks.push_back(cb);
    //if(m_callbacks.size() > 1) {
    //    std::cout << std::this_thread::get_id() << ":" << m_callbacks.size() << " " << m_name << std::endl;
    //}
    lock.unlock();
    uint8_t cmd = 1;
    //write(m_write, &cmd, sizeof(cmd));
    if(send(m_write, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

bool WatchCppThread::dispatch(uint32_t id, callback cb) {
    return dispatch(cb);
}

bool WatchCppThread::batchDispatch(const std::vector<callback>& cbs) {
    RWMtx::WriteLock lock(m_mutex);
    for(auto& i : cbs) {
        m_callbacks.push_back(i);
    }
    lock.unlock();
    uint8_t cmd = 1;
    if(send(m_write, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

void WatchCppThread::broadcast(callback cb) {
    dispatch(cb);
}

void WatchCppThread::stop() {
    RWMtx::WriteLock lock(m_mutex);
    m_callbacks.push_back(nullptr);
    if(m_thread) {
        uint8_t cmd = 0;
        //write(m_write, &cmd, sizeof(cmd));
        send(m_write, &cmd, sizeof(cmd), 0);
    }
    //if(m_data) {
    //    delete m_data;
    //    m_data = NULL;
    //}
}

void WatchCppThread::join() {
    if(m_thread) {
        m_thread->join();
        delete m_thread;
        m_thread = NULL;
    }
}

WatchCppThreadPool::WatchCppThreadPool(uint32_t size, const std::string& name, bool advance)
    :m_size(size)
    ,m_cur(0)
    ,m_name(name)
    ,m_advance(advance)
    ,m_start(false)
    ,m_total(0) {
    m_threads.resize(m_size);
    for(size_t i = 0; i < size; ++i) {
        WatchCppThread* t(new WatchCppThread(name + "_" + std::to_string(i)));
        m_threads[i] = t;
    }
}

WatchCppThreadPool::~WatchCppThreadPool() {
    for(size_t i = 0; i < m_size; ++i) {
        delete m_threads[i];
    }
}

void WatchCppThreadPool::start() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->setInitCb(m_initCb);
        m_threads[i]->start();
        m_freeWatchCppThreads.push_back(m_threads[i]);
    }
    if(m_initCb) {
        m_initCb = nullptr;
    }
    m_start = true;
    //每个空闲的foxCppThread对象都从队头取出一个函数执行，直到队列为空
    check();
}

void WatchCppThreadPool::stop() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->stop();
    }
    m_start = false;
}

void WatchCppThreadPool::join() {
    for(size_t i = 0; i < m_size; ++i) {
        m_threads[i]->join();
    }
}

void WatchCppThreadPool::releaseWatchCppThread(WatchCppThread* t) {
    do {
        RWMtx::WriteLock lock(m_mutex);
        m_freeWatchCppThreads.push_back(t);
    } while(0);
    check();
}

bool WatchCppThreadPool::dispatch(callback cb) {
    do {
        yhchaos::Atomic::addFetch(m_total, (uint64_t)1);
        RWMtx::WriteLock lock(m_mutex);
        if(!m_advance) {
            return m_threads[m_cur++ % m_size]->dispatch(cb);
        }
        m_callbacks.push_back(cb);
    } while(0);
    check();
    return true;
}

bool WatchCppThreadPool::batchDispatch(const std::vector<callback>& cbs) {
    yhchaos::Atomic::addFetch(m_total, cbs.size());
    RWMtx::WriteLock lock(m_mutex);
    if(!m_advance) {
        for(auto cb : cbs) {
            m_threads[m_cur++ % m_size]->dispatch(cb);
        }
        return true;
    }
    for(auto cb : cbs) {
        m_callbacks.push_back(cb);
    }
    lock.unlock();
    check();
    return true;
}

void WatchCppThreadPool::check() {
    do {
        if(!m_start) {
            break;
        }
        RWMtx::WriteLock lock(m_mutex);
        //必须两个都不为空
        if(m_freeWatchCppThreads.empty() || m_callbacks.empty()) {
            break;
        }

        std::shared_ptr<WatchCppThread> thr(m_freeWatchCppThreads.front(),
                std::bind(&WatchCppThreadPool::releaseWatchCppThread,
                    this, std::placeholders::_1));
        m_freeWatchCppThreads.pop_front();

        callback cb = m_callbacks.front();
        m_callbacks.pop_front();
        lock.unlock();

        if(thr->isStart()) {
            thr->dispatch(std::bind(&WatchCppThreadPool::wrapcb, this, thr, cb));
        } else {
            RWMtx::WriteLock lock(m_mutex);
            m_callbacks.push_front(cb);
        }
    } while(true);
}

void WatchCppThreadPool::wrapcb(std::shared_ptr<WatchCppThread> thr, callback cb) {
    cb();
}

bool WatchCppThreadPool::dispatch(uint32_t id, callback cb) {
    yhchaos::Atomic::addFetch(m_total, (uint64_t)1);
    return m_threads[id % m_size]->dispatch(cb);
}

WatchCppThread* WatchCppThreadPool::getRandWatchCppThread() {
    return m_threads[m_cur++ % m_size];
}

void WatchCppThreadPool::broadcast(callback cb) {
    for(size_t i = 0; i < m_threads.size(); ++i) {
        m_threads[i]->dispatch(cb);
    }
}

void WatchCppThreadPool::dump(std::ostream& os) {
    RWMtx::ReadLock lock(m_mutex);
    os << "[WatchCppThreadPool name = " << m_name << " thread_count = " << m_threads.size()
       << " tasks = " << m_callbacks.size() << " total = " << m_total
       << " advance = " << m_advance
       << "]" << std::endl;
    for(size_t i = 0; i < m_threads.size(); ++i) {
        os << "    ";
        m_threads[i]->dump(os);
    }
}

WatchCppThread* WatchCppThread::GetThis() {
    return s_thread;
}

const std::string& WatchCppThread::GetWatchCppThreadName() {
    WatchCppThread* t = GetThis();
    if(t) {
        return t->m_name;
    }

    uint64_t tid = yhchaos::GetCppThreadId();
    do {
        RWMtx::ReadLock lock(s_thread_mutex);
        auto it = s_thread_names.find(tid);
        if(it != s_thread_names.end()) {
            return it->second;
        }
    } while(0);

    do {
        RWMtx::WriteLock lock(s_thread_mutex);
        s_thread_names[tid] = "UNNAME_" + std::to_string(tid);
        return s_thread_names[tid];
    } while (0);
}

void WatchCppThread::GetAllWatchCppThreadName(std::map<uint64_t, std::string>& names) {
    RWMtx::ReadLock lock(s_thread_mutex);
    for(auto it = s_thread_names.begin();
            it != s_thread_names.end(); ++it) {
        names.insert(*it);
    }
}

void WatchCppThread::setThis() {
    m_name = m_name + "_" + std::to_string(yhchaos::GetCppThreadId());
    s_thread = this;

    RWMtx::WriteLock lock(s_thread_mutex);
    s_thread_names[yhchaos::GetCppThreadId()] = m_name;
}

void WatchCppThread::unsetThis() {
    s_thread = nullptr;
    RWMtx::WriteLock lock(s_thread_mutex);
    s_thread_names.erase(yhchaos::GetCppThreadId());
}

IWatchCppThread::ptr WatchCppThreadManager::get(const std::string& name) {
    auto it = m_threads.find(name);
    return it == m_threads.end() ? nullptr : it->second;
}

void WatchCppThreadManager::add(const std::string& name, IWatchCppThread::ptr thr) {
    m_threads[name] = thr;
}

void WatchCppThreadManager::dispatch(const std::string& name, callback cb) {
    IWatchCppThread::ptr ti = get(name);
    YHCHAOS_ASSERT(ti);
    ti->dispatch(cb);
}

void WatchCppThreadManager::dispatch(const std::string& name, uint32_t id, callback cb) {
    IWatchCppThread::ptr ti = get(name);
    YHCHAOS_ASSERT(ti);
    ti->dispatch(id, cb);
}

void WatchCppThreadManager::batchDispatch(const std::string& name, const std::vector<callback>& cbs) {
    IWatchCppThread::ptr ti = get(name);
    YHCHAOS_ASSERT(ti);
    ti->batchDispatch(cbs);
}

void WatchCppThreadManager::broadcast(const std::string& name, callback cb) {
    IWatchCppThread::ptr ti = get(name);
    YHCHAOS_ASSERT(ti);
    ti->broadcast(cb);
}

void WatchCppThreadManager::dumpWatchCppThreadStatus(std::ostream& os) {
    os << "WatchCppThreadManager: " << std::endl;
    for(auto it = m_threads.begin();
            it != m_threads.end(); ++it) {
        it->second->dump(os);
    }

    os << "All WatchCppThreads:" << std::endl;
    std::map<uint64_t, std::string> names;
    WatchCppThread::GetAllWatchCppThreadName(names);
    for(auto it = names.begin();
            it != names.end(); ++it) {
        os << std::setw(30) << it->first
           << ": " << it->second << std::endl;
    }
}

void WatchCppThreadManager::init() {
    //m=[string->[string->string]]=[name->["num"/"advance"->string]]
    auto m = g_thread_info_set->getValue();
    for(auto i : m) {
        auto num = yhchaos::GetParamValue(i.second, "num", 0);
        auto name = i.first;
        auto advance = yhchaos::GetParamValue(i.second, "advance", 0);
        if(num <= 0) {
            YHCHAOS_LOG_ERROR(g_logger) << "thread pool:" << name
                        << " num:" << num
                        << " advance:" << advance
                        << " invalid";
            continue;
        }
        if(num == 1) {
            m_threads[i.first] = WatchCppThread::ptr(new WatchCppThread(i.first));
            YHCHAOS_LOG_INFO(g_logger) << "init thread : " << i.first;
        } else {
            m_threads[i.first] = WatchCppThreadPool::ptr(new WatchCppThreadPool(
                            num, name, advance));
            YHCHAOS_LOG_INFO(g_logger) << "init thread pool:" << name
                       << " num:" << num
                       << " advance:" << advance;
        }
    }
}

void WatchCppThreadManager::start() {
    for(auto i : m_threads) {
        YHCHAOS_LOG_INFO(g_logger) << "thread: " << i.first << " start begin";
        i.second->start();
        YHCHAOS_LOG_INFO(g_logger) << "thread: " << i.first << " start end";
    }
}

void WatchCppThreadManager::stop() {
    for(auto i : m_threads) {
        YHCHAOS_LOG_INFO(g_logger) << "thread: " << i.first << " stop begin";
        i.second->stop();
        YHCHAOS_LOG_INFO(g_logger) << "thread: " << i.first << " stop end";
    }
    for(auto i : m_threads) {
        YHCHAOS_LOG_INFO(g_logger) << "thread: " << i.first << " join begin";
        i.second->join();
        YHCHAOS_LOG_INFO(g_logger) << "thread: " << i.first << " join end";
    }
}

}
