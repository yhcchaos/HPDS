#include "worker.h"
#include "appconfig.h"
#include "util.h"

namespace yhchaos {

static yhchaos::AppConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_worker_config
    = yhchaos::AppConfig::SearchFor("workers", std::map<std::string, std::map<std::string, std::string> >(), "worker config");

CoroutineGroup::CoroutineGroup(uint32_t batch_size, yhchaos::CoScheduler* s)
    :m_batchSize(batch_size)
    ,m_finish(false)
    ,m_coscheduler(s)
    ,m_sem(batch_size) {
}

CoroutineGroup::~CoroutineGroup() {
    waitAll();
}

void CoroutineGroup::coschedule(std::function<void()> cb, int thread) {
    m_sem.wait();
    m_coscheduler->coschedule(std::bind(&CoroutineGroup::doWork
                          ,shared_from_this(), cb), thread);
}

void CoroutineGroup::doWork(std::function<void()> cb) {
    cb();
    m_sem.notify();
}

void CoroutineGroup::waitAll() {
    if(!m_finish) {
        m_finish = true;
        for(uint32_t i = 0; i < m_batchSize; ++i) {
            m_sem.wait();
        }
    }
}

CoSchedulerManager::CoSchedulerManager()
    :m_stop(false) {
}

void CoSchedulerManager::add(CoScheduler::ptr s) {
    m_datas[s->getName()].push_back(s);
}

CoScheduler::ptr CoSchedulerManager::get(const std::string& name) {
    auto it = m_datas.find(name);
    if(it == m_datas.end()) {
        return nullptr;
    }
    if(it->second.size() == 1) {
        return it->second[0];
    }
    return it->second[rand() % it->second.size()];
}

IOCoScheduler::ptr CoSchedulerManager::getAsIOCoScheduler(const std::string& name) {
    return std::dynamic_pointer_cast<IOCoScheduler>(get(name));
}

bool CoSchedulerManager::init(const std::map<std::string, std::map<std::string, std::string> >& v) {
    for(auto& i : v) {
        std::string name = i.first;
        int32_t thread_num = yhchaos::GetParamValue(i.second, "thread_num", 1);
        int32_t worker_num = yhchaos::GetParamValue(i.second, "worker_num", 1);//默认是1,同一类型调度器的个数

        for(int32_t x = 0; x < worker_num; ++x) {
            CoScheduler::ptr s;
            if(!x) {
                s = std::make_shared<IOCoScheduler>(thread_num, false, name);//false来创建调度器，没有rootCoroutine
            } else {
                s = std::make_shared<IOCoScheduler>(thread_num, false, name + "-" + std::to_string(x));
            }
            add(s);
        }
    }
    m_stop = m_datas.empty();
    return true;
}

bool CoSchedulerManager::init() {
    auto workers = g_worker_config->getValue();
    return init(workers);
}

void CoSchedulerManager::stop() {
    if(m_stop) {
        return;
    }
    for(auto& i : m_datas) {
        for(auto& n : i.second) {
            n->coschedule([](){});
            n->stop();
        }
    }
    m_datas.clear();
    m_stop = true;
}

uint32_t CoSchedulerManager::getCount() {
    return m_datas.size();
}

std::ostream& CoSchedulerManager::dump(std::ostream& os) {
    for(auto& i : m_datas) {
        for(auto& n : i.second) {
            n->dump(os) << std::endl;
        }
    }
    return os;
}

}
