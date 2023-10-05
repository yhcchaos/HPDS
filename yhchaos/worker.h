#ifndef __YHCHAOS_WORKER_H__
#define __YHCHAOS_WORKER_H__

#include "mtx.h"
#include "singleton.h"
#include "log.h"
#include "iocoscheduler.h"

namespace yhchaos {
//持有这个对象的协程最多一次只能调度m_batchSize个任务，如果调度的任务超过m_batchSize个，那么该协程协程就会被挂起，直到有任务完成，才会继续该协程进行调度
class CoroutineGroup : Noncopyable, public std::enable_shared_from_this<CoroutineGroup> {
public:
    typedef std::shared_ptr<CoroutineGroup> ptr;
    static CoroutineGroup::ptr Create(uint32_t batch_size, yhchaos::CoScheduler* s = yhchaos::CoScheduler::GetThis()) {
        return std::make_shared<CoroutineGroup>(batch_size, s);
    }

    CoroutineGroup(uint32_t batch_size, yhchaos::CoScheduler* s = yhchaos::CoScheduler::GetThis());
    ~CoroutineGroup();
    void coschedule(std::function<void()> cb, int thread = -1);
    void waitAll();
private:
    void doWork(std::function<void()> cb);
private:
    uint32_t m_batchSize;
    bool m_finish;
    CoScheduler* m_coscheduler;
    CoroutineSem m_sem;
};

class CoSchedulerManager {
public:
    CoSchedulerManager();
    void add(CoScheduler::ptr s);
    CoScheduler::ptr get(const std::string& name);
    IOCoScheduler::ptr getAsIOCoScheduler(const std::string& name);
    template<class CoroutineOrCb>
    void coschedule(const std::string& name, CoroutineOrCb fc, int thread = -1) {
        auto s = get(name);
        if(s) {
            s->coschedule(fc, thread);
        } else {
            static yhchaos::Logger::ptr s_logger = YHCHAOS_LOG_NAME("system");
            YHCHAOS_LOG_ERROR(s_logger) << "coschedule name=" << name
                << " not exists";
        }
    }
    //随机返回一个name对应的调度器，然后调度begin到end的协程
    template<class Iter>
    void coschedule(const std::string& name, Iter begin, Iter end) {
        auto s = get(name);
        if(s) {
            s->coschedule(begin, end);
        } else {
            static yhchaos::Logger::ptr s_logger = YHCHAOS_LOG_NAME("system");
            YHCHAOS_LOG_ERROR(s_logger) << "coschedule name=" << name
                << " not exists";
        }
    }
    //根据g_worker_config={names->[thread_num/work_num->num]},初始化m_datas
    bool init();
    //对于每一个name，创建worker_num(use-caller=false)个调度器，名字分别为name-i(0-worker_num)，
    //调度器的线程数量为thread_num
    bool init(const std::map<std::string, std::map<std::string, std::string> >& v);
    void stop();

    bool isStoped() const { return m_stop;}
    std::ostream& dump(std::ostream& os);
    //返回m_datas中不同调度器名字的个数
    uint32_t getCount();
private:
    //两个调度器：{"io（名字）":{thread_num:4}（线程数量）,"accept":{thread_num:1}
    std::map<std::string, std::vector<CoScheduler::ptr> > m_datas;//空
    bool m_stop;//false, m_stop = m_datas.empty();
};

typedef yhchaos::Singleton<CoSchedulerManager> CoSchedulerMgr;

}

#endif
