#ifndef __YHCHAOS_DB_WATCH_THREAD_H__
#define __YHCHAOS_DB_WATCH_THREAD_H__

#include <thread>
#include <vector>
#include <list>
#include <map>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>

#include "yhchaos/singleton.h"
#include "yhchaos/mtx.h"

namespace yhchaos {

class WatchCppThread;
class IWatchCppThread {
public:
    typedef std::shared_ptr<IWatchCppThread> ptr;
    typedef std::function<void()> callback;

    virtual ~IWatchCppThread(){};
    virtual bool dispatch(callback cb) = 0;//调度，分派，派遣，发送
    virtual bool dispatch(uint32_t id, callback cb) = 0;
    virtual bool batchDispatch(const std::vector<callback>& cbs) = 0;
    virtual void broadcast(callback cb) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void join() = 0;
    virtual void dump(std::ostream& os) = 0;
    virtual uint64_t getTotal() = 0;
};
//构造函数创建了一对unix套接字,fd[0]用于读，fd[1]用于写
//当调用start()时，创建一个线程m_thread执行thread_cb()，在thread_cb()中用event_base_loop阻塞监听fd[0]
//主线程可以通过向fd[1]写来唤醒m_thread
//通过向m_callcb列表添加回调函数的形式让m_thread被唤醒时执行回调函数
class WatchCppThread : public IWatchCppThread {
public:
    typedef std::shared_ptr<WatchCppThread> ptr;
    //void()
    typedef IWatchCppThread::callback callback;
    typedef std::function<void (WatchCppThread*)> init_cb;
    WatchCppThread(const std::string& name = "", struct event_base* base = NULL);
    ~WatchCppThread();


    //返回全局线程局部不变量s_thread
    static WatchCppThread* GetThis();

    /**
     * @brief 返回当前线程的fox thread名字
     * @return 
     *  1. 如果线程局部变量s_thread不为空，返回s_thread->m_name
     *  2. 如果s_thread为空，就获得当前线程的线程tid，返回全局变量s_thread_names[tid]
     *  3. 如果s_thread为空且tid不在s_thread_names中，就创建s_thread_names[tid]=UNNAME_tid
     *     然后返回s_thread_names[tid]
    */
    static const std::string& GetWatchCppThreadName();

    //将全局变量s_thread_names复制到引用names中
    static void GetAllWatchCppThreadName(std::map<uint64_t, std::string>& names);

    //设置m_name=m_name+当前线程tid, s_thread=this, tid:m_name添加到s_thread_neames
    void setThis();

    //s_thread=nullptr, 删除s_thread_names[tid]
    void unsetThis();

    //创建一个线程执行WatchCppThread::thread_cb并赋值个m_thread，m_start=true
    void start();

    virtual bool dispatch(callback cb);
    virtual bool dispatch(uint32_t id, callback cb);
    virtual bool batchDispatch(const std::vector<callback>& cbs);
    virtual void broadcast(callback cb);

    void join();
    void stop();
    bool isStart() const { return m_start;}

    struct event_base* getBase() { return m_base;}
    std::thread::id getId() const;

    void* getData(const std::string& name);
    template<class T>
    T* getData(const std::string& name) {
        return (T*)getData(name);
    }
    void setData(const std::string& name, void* v);

    void setInitCb(init_cb v) { m_initCb = v;}

    void dump(std::ostream& os);
    virtual uint64_t getTotal() { return m_total;}
private:
    void thread_cb();
    static void read_cb(evutil_socket_t sock, short which, void* args);
private:
    evutil_socket_t m_read;//Unix套接字对fd[0]读端
    evutil_socket_t m_write;//fd[1]
    struct event_base* m_base;//base或者event_base_new()
    //m_event = event_new(m_base, m_read, EV_READ | EV_PERSIST, read_cb, this)
    struct event* m_event;//f[0]的读事件
    std::thread* m_thread;//NULL，在start函数中设置为thread_cb，监听m_base
    yhchaos::RWMtx m_mutex;//保护m_callbacks
    std::list<callback> m_callbacks;

    std::string m_name;//name
    init_cb m_initCb;

    std::map<std::string, void*> m_datas;

    bool m_working;//false，在start()中设置为true
    bool m_start;//false，表示当前正在执行回调函数
    uint64_t m_total;//0，总共执行的回调函数的个数
};

class WatchCppThreadPool : public IWatchCppThread {
public:
    typedef std::shared_ptr<WatchCppThreadPool> ptr;
    typedef IWatchCppThread::callback callback;
    //构造函数创建size个WatchCppThread对象，添加到m_trheads线程池中
    WatchCppThreadPool(uint32_t size, const std::string& name = "", bool advance = false);
    ~WatchCppThreadPool();
    //运行m_threads中的每个WatchCppThread对象的start()，将他们的m_initCB设为WatchCppThreadPool::init_cb
    void start();
    void stop();
    //等待m_threads中的每个WatchCppThread对象的m_thread执行完毕
    void join();

    /**
     * 如果m_advance=false，那么就选择m_threads[m_cur++ % m_size]->dispatch(cb)去执行cb
     * 如果m_advance=true，那么就让m_freeWatchCppThreads的队头WatchCppThread对象执行m_callbacks中的队头回调函数
     * ，将他们分别出队列，一直到有一个为空
    */
    bool dispatch(callback cb);
    bool batchDispatch(const std::vector<callback>& cb);
    //指定线程执行
    bool dispatch(uint32_t id, callback cb);

    WatchCppThread* getRandWatchCppThread();
    void setInitCb(WatchCppThread::init_cb v) { m_initCb = v;}

    void dump(std::ostream& os);
    //让m_threads中的每个WatchCppThread对象dispatch函数cb
    void broadcast(callback cb);
    virtual uint64_t getTotal() { return m_total;}
private:
    //将t加入空闲队列中，然后进行一次cb的调度
    void releaseWatchCppThread(WatchCppThread* t);
    void check();

    void wrapcb(std::shared_ptr<WatchCppThread>, callback cb);
private:
    uint32_t m_size;//size
    uint32_t m_cur;//0
    std::string m_name;//name=""
    bool m_advance;//advance=false
    bool m_start;//false
    RWMtx m_mutex;//保护m_threads,m_callback和m_freeWatchCppThreads
    std::list<callback> m_callbacks;
    std::vector<WatchCppThread*> m_threads;//WatchCppThread线程池=name + "_" + std::to_string(i)
    //空闲的WatchCppThread线程池
    std::list<WatchCppThread*> m_freeWatchCppThreads;
    WatchCppThread::init_cb m_initCb;
    //没dispatch一个回调函数就m_total++
    uint64_t m_total;//0
};

class WatchCppThreadManager {
public:
    typedef IWatchCppThread::callback callback;
    void dispatch(const std::string& name, callback cb);
    void dispatch(const std::string& name, uint32_t id, callback cb);
    void batchDispatch(const std::string& name, const std::vector<callback>& cbs);
    void broadcast(const std::string& name, callback cb);

    void dumpWatchCppThreadStatus(std::ostream& os);
    //根据g_thread_info_set散列[string->[string->string]]=[name->["num"/"advance"->string]]
    //来创建fox线程池和fox线程对象并添加到m_threads中
    void init();
    //执行m_threads中的每个WatchCppThread对象或foxCppThreadPoling的start()
    void start();
    void stop();

    IWatchCppThread::ptr get(const std::string& name);
    void add(const std::string& name, IWatchCppThread::ptr thr);
private:
    std::map<std::string, IWatchCppThread::ptr> m_threads;
};

typedef Singleton<WatchCppThreadManager> WatchCppThreadMgr;

}
#endif
