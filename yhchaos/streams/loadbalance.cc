#include "loadbalance.h"
#include "yhchaos/log.h"
#include "yhchaos/worker.h"
#include "yhchaos/macro.h"
#include <math.h>

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

HolderStats HolderStatsSet::getTotal() {
    HolderStats rt;
    for(auto& i : m_stats) {
#define XX(f) rt.f += i.f
        XX(m_usedTime);
        XX(m_total);
        XX(m_doing);
        XX(m_timeouts);
        XX(m_oks);
        XX(m_errs);
#undef XX
    }
    return rt;
}

std::string HolderStats::toString() {
    std::stringstream ss;
    ss << "[Stat total=" << m_total
       << " used_time=" << m_usedTime
       << " doing=" << m_doing
       << " timeouts=" << m_timeouts
       << " oks=" << m_oks
       << " errs=" << m_errs
       << " oks_rate=" << (m_total ? (m_oks * 100.0 / m_total) : 0)
       << " errs_rate=" << (m_total ? (m_errs * 100.0 / m_total) : 0)
       << " avg_used=" << (m_oks ? (m_usedTime * 1.0 / m_oks) : 0)
       << " weight=" << getWeight(1)
       << "]";
    return ss.str();
}

void LBItem::close() {
    if(m_stream) {
        auto stream = m_stream;
        yhchaos::CoSchedulerMgr::GetInstance()->coschedule("service_io", [stream](){
            stream->close();
        });
    }
}

bool LBItem::isValid() {
    return m_stream && m_stream->isConnected();
}

std::string LBItem::toString() {
    std::stringstream ss;
    ss << "[Item id=" << m_id
       << " weight=" << getWeight();
    if(!m_stream) {
        ss << " stream=null";
    } else {
        ss << " stream=[" << m_stream->getRemoteNetworkAddressString()
           << " is_connected=" << m_stream->isConnected() << "]";
    }
    ss << m_stats.getTotal().toString() << "]";
    //float w = 0;
    //float w2 = 0;
    //for(uint64_t n = 0; n < 5; ++n) {
    //    if(n) {
    //        ss << ",";
    //    } else {
    //        ss << m_stats.get(time(0) - n).toString();
    //    }
    //    w += m_stats.get(time(0) - n).getWeight();
    //    w2 += m_stats.get(time(0) - n).getWeight() * (1 - n * 0.1);
    //    ss << m_stats.get(time(0) - n).getWeight();
    //}
    //ss << " w=" << w;
    //ss << " w2=" << w2;
    return ss.str();
}

LBItem::ptr LB::getById(uint64_t id) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_datas.find(id);
    return it == m_datas.end() ? nullptr : it->second;
}

void LB::add(LBItem::ptr v) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas[v->getId()] = v;
    initNolock();
}

void LB::del(LBItem::ptr v) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas.erase(v->getId());
    initNolock();
}

void LB::update(const std::unordered_map<uint64_t, LBItem::ptr>& adds
                        ,std::unordered_map<uint64_t, LBItem::ptr>& dels) {
    RWMtxType::WriteLock lock(m_mutex);
    //如果dels中的元素也在m_datas中，那么将其从m_datas中删除，并将其放入dels中
    for(auto& i : dels) {
        auto it = m_datas.find(i.first);
        if(it != m_datas.end()) {
            i.second = it->second;
            m_datas.erase(it);
        }
    }
    for(auto& i : adds) {
        m_datas[i.first] = i.second;
    }
    initNolock();
}

void LB::set(const std::vector<LBItem::ptr>& vs) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas.clear();
    for(auto& i : vs){
        m_datas[i->getId()] = i;
    }
    initNolock();
}

void LB::init() {
    RWMtxType::WriteLock lock(m_mutex);
    initNolock();
}

std::string LB::statusString(const std::string& prefix) {
    RWMtxType::ReadLock lock(m_mutex);
    decltype(m_datas) datas = m_datas;
    lock.unlock();
    std::stringstream ss;
    ss << prefix << "init_time: " << yhchaos::Time2Str(m_lastInitTime / 1000) << std::endl;
    for(auto& i : datas) {
        ss << prefix << i.second->toString() << std::endl;
    }
    return ss.str();
}

void LB::checkInit() {
    uint64_t ts = yhchaos::GetCurrentMS();
    if(ts - m_lastInitTime > 500) {
        init();
        m_lastInitTime = ts;
    }
}

void RoundRobinLB::initNolock() {
    decltype(m_items) items;
    for(auto& i : m_datas){
        if(i.second->isValid()) {
            items.push_back(i.second);
        }
    }
    items.swap(m_items);
}

LBItem::ptr RoundRobinLB::get(uint64_t v) {
    checkInit();
    RWMtxType::ReadLock lock(m_mutex);
    if(m_items.empty()) {
        return nullptr;
    }
    uint32_t r = (v == (uint64_t)-1 ? rand() : v) % m_items.size();
    for(size_t i = 0; i < m_items.size(); ++i) {
        auto& h = m_items[(r + i) % m_items.size()];
        if(h->isValid()) {
            return h;
        }
    }
    return nullptr;
}

FairLBItem::ptr WeightLB::getAsFair() {
    auto item = get();
    if(item) {
        return std::static_pointer_cast<FairLBItem>(item);
    }
    return nullptr;
}

LBItem::ptr WeightLB::get(uint64_t v) {
    checkInit();
    RWMtxType::ReadLock lock(m_mutex);
    int32_t idx = getIdx(v);
    if(idx == -1) {
        return nullptr;
    }

    //TODO fix weight
    for(size_t i = 0; i < m_items.size(); ++i) {
        auto& h = m_items[(idx + i) % m_items.size()];
        if(h->isValid()) {
            return h;
        }
    }
    return nullptr;
}

void WeightLB::initNolock() {
    decltype(m_items) items;
    for(auto& i : m_datas){
        if(i.second->isValid()) {
            items.push_back(i.second);
        }
    }
    items.swap(m_items);

    int64_t total = 0;
    m_weights.resize(m_items.size());
    for(size_t i = 0; i < m_items.size(); ++i) {
        total += m_items[i]->getWeight();
        m_weights[i] = total;
    }
}

int32_t WeightLB::getIdx(uint64_t v) {
    if(m_weights.empty()) {
        return -1;
    }
    int64_t total = *m_weights.rbegin();
    uint64_t dis = (v == (uint64_t)-1 ? rand() : v) % total;
    auto it = std::upper_bound(m_weights.begin()
                ,m_weights.end(), dis);
    YHCHAOS_ASSERT(it != m_weights.end());
    return std::distance(m_weights.begin(), it);
}

void HolderStats::clear() {
    m_usedTime = 0;
    m_total = 0;
    m_doing = 0;
    m_timeouts = 0;
    m_oks = 0;
    m_errs = 0;
}

float HolderStats::getWeight(float rate) {
    //if(m_total == 0) {
    //    return 0.1;
    //}
    float base = m_total + 20;
    return std::min((m_oks * 1.0 / (m_usedTime + 1)) * 2.0, 50.0)
        //跟超时任务率，正在执行任务率，出错任务率成反比，跟单位时间内成功任务数量成正比
        * (1 - 4.0 * m_timeouts / base) 
        * (1 - 1 * m_doing / base)
        * (1 - 10.0 * m_errs / base) * rate;
    //return std::min((m_oks * 1.0 / (m_usedTime + 1)) * 10.0, 100.0)
    //    * (1 - (2.0 * pow(m_timeouts, 1.3) / base))
    //    * (1 - (1.0 * pow(m_doing, 1.1) / base))
    //    * (1 - (4.0 * pow(m_errs, 1.5) / base)) * rate;
    //return std::min(((m_oks + 1) * 1.0 / (m_usedTime + 1)) * 10.0, 100.0)
    //    * std::min((base / (m_timeouts * 3.0 + 1)) / 100.0, 10.0)
    //    * std::min((base / ( m_doing * 1.0 + 1)) / 100.0, 10.0)
    //    * std::min((base / (m_errs * 5.0 + 1)) / 100.0, 10.0);
}

HolderStatsSet::HolderStatsSet(uint32_t size) {
    m_stats.resize(size);
}
//将上一次m_lastUpdateTime+1到now的HolderStats清空，然后返回now对应的HolderStats
void HolderStatsSet::init(const uint32_t& now) {
    if(m_lastUpdateTime < now) {
        for(uint32_t t = m_lastUpdateTime + 1, i = 0;
                t <= now && i < m_stats.size(); ++t, ++i) {
            m_stats[t % m_stats.size()].clear();
        }
        m_lastUpdateTime = now;
    }
}

HolderStats& HolderStatsSet::get(const uint32_t& now) {
    //这是为了确保 m_stats 中的数据已经准备好用于记录和统计。
    init(now);
    return m_stats[now % m_stats.size()];
}

float HolderStatsSet::getWeight(const uint32_t& now) {
    init(now);
    float v = 0;
    for(size_t i = 1; i < m_stats.size(); ++i) {
        v += m_stats[(now - i) % m_stats.size()].getWeight(1 - 0.1 * i);
    }
    return v;
    //return getTotal().getWeight(1.0);
}

int32_t FairLBItem::getWeight() {
    int32_t v = m_weight * m_stats.getWeight();
    if(m_stream->isConnected()) {
        return v > 1 ? v : 1;
    }
    return 1;
}

HolderStats& LBItem::get(const uint32_t& now) {
    return m_stats.get(now);
}

LBItem::ptr FairLB::get() {
    RWMtxType::ReadLock lock(m_mutex);
    int32_t idx = getIdx();
    if(idx == -1) {
        return nullptr;
    }

    //TODO fix weight
    for(size_t i = 0; i < m_items.size(); ++i) {
        auto& h = m_items[(idx + i) % m_items.size()];
        if(h->isValid()) {
            return h;
        }
    }
    return nullptr;
}

void FairLB::initNolock() {
    decltype(m_items) items;
    for(auto& i : m_datas){
        items.push_back(i.second);
    }
    items.swap(m_items);

    m_weights.resize(m_items.size());
    int32_t total = 0;
    for(size_t i = 0; i < m_items.size(); ++i) {
        total += m_items[i]->getWeight();
        m_weights[i] = total;
    }
}

int32_t FairLB::getIdx() {
    if(m_weights.empty()) {
        return -1;
    }
    int32_t total = *m_weights.rbegin();
    auto it = std::upper_bound(m_weights.begin()
                ,m_weights.end(), rand() % total);
    return std::distance(it, m_weights.begin());
}

SDLB::SDLB(ISD::ptr sd)
    :m_sd(sd) {
}

LB::ptr SDLB::get(const std::string& domain, const std::string& service, bool auto_create) {
    do {
        RWMtxType::ReadLock lock(m_mutex);
        auto it = m_datas.find(domain);
        if(it == m_datas.end()) {
            break;
        }
        auto iit = it->second.find(service);
        if(iit == it->second.end()) {
            break;
        }
        return iit->second;
    } while(0);

    if(!auto_create) {
        return nullptr;
    }

    auto type = getType(domain, service);

    auto lb = createLB(type);
    RWMtxType::WriteLock lock(m_mutex);
    m_datas[domain][service] = lb;
    lock.unlock();
    return lb;
}


ILB::Type SDLB::getType(const std::string& domain, const std::string& service) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_types.find(domain);
    if(it == m_types.end()) {
        return m_defaultType;
    }
    auto iit = it->second.find(service);
    if(iit == it->second.end()) {
        return m_defaultType;
    }
    return iit->second;
}

LB::ptr SDLB::createLB(ILB::Type type) {
    if(type == ILB::ROUNDROBIN) {
        return RoundRobinLB::ptr(new RoundRobinLB);
    } else if(type == ILB::WEIGHT) {
        return WeightLB::ptr(new WeightLB);
    } else if(type == ILB::FAIR) {
        return WeightLB::ptr(new WeightLB);
    }
    return nullptr;
}

LBItem::ptr SDLB::createLBItem(ILB::Type type) {
    LBItem::ptr item;
    if(type == ILB::ROUNDROBIN) {
        item.reset(new LBItem);
    } else if(type == ILB::WEIGHT) {
        item.reset(new LBItem);
    } else if(type == ILB::FAIR) {
        item.reset(new FairLBItem);
    }
    return item;
}

void SDLB::onServiceChange(const std::string& domain, const std::string& service
                            ,const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& old_value
                            ,const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& new_value) {
    YHCHAOS_LOG_INFO(g_logger) << "onServiceChange domain=" << domain
                             << " service=" << service;
    auto type = getType(domain, service);
    auto lb = get(domain, service, true);
    //m_id->ServiceItemInfo
    std::unordered_map<uint64_t, ServiceItemInfo::ptr> add_values;
    //m_id->LBItem,要删除的流的负载均衡对象
    std::unordered_map<uint64_t, LBItem::ptr> del_infos;
    //将出现在old_value中但new_value中没有的m_id，将其放入del_infos中，表示该流及其负载均衡信息都要删除
    for(auto& i : old_value) {
        if(new_value.find(i.first) == new_value.end()) {
            del_infos[i.first];
        }
    }
    //将出现在new_value中但old_value中没有的m_id，将其放入add_values中，注意我们在service discovery中的queryData是以空值创建new_value
    for(auto& i : new_value) {
        if(old_value.find(i.first) == old_value.end()) {
            add_values.insert(i);
        }
    }
    //m_id->LBItem，为新添加的流创建负载均衡对象
    std::unordered_map<uint64_t, LBItem::ptr> add_infos;
    for(auto& i : add_values) {
        //利用回调函数m_cb根据add_values中的每个ServiceItemInfo创建对应的SockStream
        auto stream = m_cb(i.second);
        if(!stream) {
            YHCHAOS_LOG_ERROR(g_logger) << "create stream fail, " << i.second->toString();
            continue;
        }
        
        LBItem::ptr lditem = createLBItem(type);
        lditem->setId(i.first);
        lditem->setStream(stream);
        lditem->setWeight(10000);

        add_infos[i.first] = lditem;
    }
    //添加新的负载均衡对象，删除del_infos中的负载均衡对象
    lb->update(add_infos, del_infos);
    //将删除的负载均衡对象的流关闭
    for(auto& i : del_infos) {
        if(i.second) {
            i.second->close();
        }
    }
}

void SDLB::start() {
    m_sd->setServiceCallback(std::bind(&SDLB::onServiceChange, this
                ,std::placeholders::_1
                ,std::placeholders::_2
                ,std::placeholders::_3
                ,std::placeholders::_4));
    m_sd->start();
}

void SDLB::stop() {
    m_sd->stop();
}
//{aylar.top:{all:fair}}
void SDLB::initConf(const std::unordered_map<std::string
                            ,std::unordered_map<std::string,std::string> >& confs) {
    decltype(m_types) types;//{aylar.top:{all:FAIR}}
    std::unordered_map<std::string, std::unordered_set<std::string> > query_infos;//{aylar.top:[all]}
    for(auto& i : confs) {
        for(auto& n : i.second) {
            ILB::Type t = ILB::FAIR;
            if(n.second == "round_robin") {
                t = ILB::ROUNDROBIN;
            } else if(n.second == "weight") {
                t = ILB::WEIGHT;
            }
            types[i.first][n.first] = t;
            query_infos[i.first].insert(n.first);
        }
    }
    m_sd->setQuerySvr(query_infos);
    RWMtxType::WriteLock lock(m_mutex);
    types.swap(m_types);
    lock.unlock();
}

std::string SDLB::statusString() {
    RWMtxType::ReadLock lock(m_mutex);
    decltype(m_datas) datas = m_datas;
    lock.unlock();
    std::stringstream ss;
    for(auto& i : datas) {
        ss << i.first << ":" << std::endl;
        for(auto& n : i.second) {
            ss << "\t" << n.first << ":" << std::endl;
            ss << n.second->statusString("\t\t") << std::endl;
        }
    }
    return ss.str();
}

}
