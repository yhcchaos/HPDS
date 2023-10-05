#ifndef __YHCHAOS_STREAMS_SOCK_STREAM_POOL_H__
#define __YHCHAOS_STREAMS_SOCK_STREAM_POOL_H__

#include "yhchaos/streams/sock_stream.h"
#include "yhchaos/mtx.h"
#include "yhchaos/util.h"
#include "yhchaos/streams/service_discovery.h"
#include <vector>
#include <unordered_map>

namespace yhchaos {

class HolderStatsSet;
class HolderStats {
friend class HolderStatsSet;
public:
    uint32_t getUsedTime() const { return m_usedTime; }
    uint32_t getTotal() const { return m_total; }
    uint32_t getDoing() const { return m_doing; }
    uint32_t getTimeouts() const { return m_timeouts; }
    uint32_t getOks() const { return m_oks; }
    uint32_t getErrs() const { return m_errs; }

    uint32_t incUsedTime(uint32_t v) { return yhchaos::Atomic::addFetch(m_usedTime ,v);}
    //增加总的任务数量，通常用于记录新任务的到达。
    uint32_t incTotal(uint32_t v) { return yhchaos::Atomic::addFetch(m_total, v);}
    uint32_t incDoing(uint32_t v) { return yhchaos::Atomic::addFetch(m_doing, v);}
    uint32_t incTimeouts(uint32_t v) { return yhchaos::Atomic::addFetch(m_timeouts, v);}
    uint32_t incOks(uint32_t v) { return yhchaos::Atomic::addFetch(m_oks, v);}
    uint32_t incErrs(uint32_t v) { return yhchaos::Atomic::addFetch(m_errs, v);}

    uint32_t decDoing(uint32_t v) { return yhchaos::Atomic::subFetch(m_doing, v);}
    void clear();
    float getWeight(float rate = 1.0f);

    std::string toString();
private:
    uint32_t m_usedTime = 0;
    uint32_t m_total = 0;
    uint32_t m_doing = 0;
    uint32_t m_timeouts = 0;
    uint32_t m_oks = 0;
    uint32_t m_errs = 0;
};
class HolderStatsSet {
public:
    HolderStatsSet(uint32_t size = 5);
    HolderStats& get(const uint32_t& now = time(0));
    float getWeight(const uint32_t& now = time(0));
    HolderStats getTotal();
private:
    ////将上一次m_lastUpdateTime+1到now的HolderStats清空
    void init(const uint32_t& now);
private:
    uint32_t m_lastUpdateTime = 0; //seconds
    std::vector<HolderStats> m_stats;
};
//某个流m_stream的（负载均衡信息）HolderStatsSet
//来记录这个流过去一定长度时间点的统计信息
class LBItem {
public:
    typedef std::shared_ptr<LBItem> ptr;
    virtual ~LBItem() {}

    SockStream::ptr getStream() const { return m_stream;}
    void setStream(SockStream::ptr v) { m_stream = v;}

    void setId(uint64_t v) { m_id = v;}
    uint64_t getId() const { return m_id;}

    HolderStats& get(const uint32_t& now = time(0));

    template<class T>
    std::shared_ptr<T> getStreamAs() {
        return std::dynamic_pointer_cast<T>(m_stream);
    }

    virtual int32_t getWeight() { return m_weight;}
    void setWeight(int32_t v) { m_weight = v;}

    virtual bool isValid();
    void close();

    std::string toString();
protected:
    uint64_t m_id = 0;//ServiceItemInfo::m_id
    SockStream::ptr m_stream;
    int32_t m_weight = 0;//这个流的权重，lditem->setWeight(10000)
    HolderStatsSet m_stats;
};

class ILB {
public:
    enum Type {
        ROUNDROBIN = 1,
        WEIGHT = 2,
        FAIR = 3
    };

    enum Error {
        NO_SERVICE = -101,
        NO_CONNECTION = -102,
    };
    typedef std::shared_ptr<ILB> ptr;
    virtual ~ILB() {}
    virtual LBItem::ptr get(uint64_t v = -1) = 0;
};
//多个流的负载均衡信息LBItem，放在数组中
class LB : public ILB {
public:
    typedef yhchaos::RWMtx RWMtxType;
    typedef std::shared_ptr<LB> ptr;
    void add(LBItem::ptr v);
    void del(LBItem::ptr v);
    void set(const std::vector<LBItem::ptr>& vs);

    LBItem::ptr getById(uint64_t id);
    /**
     * @brief adds中的元素添加到m_datas中，dels中的元素从m_datas中删除，并把dels的值替换为m_datas中对应元素原先的值
    */
    void update(const std::unordered_map<uint64_t, LBItem::ptr>& adds
                ,std::unordered_map<uint64_t, LBItem::ptr>& dels);
    void init();

    std::string statusString(const std::string& prefix);
protected:
    virtual void initNolock() = 0;
    //最多间隔500ms初始化一次
    void checkInit();
protected:
    //保护m_datas
    RWMtxType m_mutex;
    //socketStream的{LBItem::m_id, LBItem}池，每个LBItem对应一个socketStream
    std::unordered_map<uint64_t, LBItem::ptr> m_datas;
    uint64_t m_lastInitTime = 0;
};

class RoundRobinLB : public LB {
public:
    typedef std::shared_ptr<RoundRobinLB> ptr;

    /**
     * @brief 根据v获取一个某个流的负载均衡信息
     * @param[in] v 用于计算m_datas中的下标,-1或某个值
    */
    virtual LBItem::ptr get(uint64_t v = -1) override;

protected:
    //将m_datas中还在连接的元素添加到m_items中
    virtual void initNolock();
protected:
    std::vector<LBItem::ptr> m_items;
};
//对于一个流来说，除了给每个时间点的状态信息一个权重，还给了每个流一个特定的权重，权重取这两个权重的挤
class FairLB;
class FairLBItem : public LBItem {
friend class FairLB;
public:
    typedef std::shared_ptr<FairLBItem> ptr;

    void clear();
    virtual int32_t getWeight();
};

//利用每个流的权重m_weight来随机选取一个流
class WeightLB : public LB {
public:
    typedef std::shared_ptr<WeightLB> ptr;
    //返回m_items中getIdx(v)后面第一个有效的LBItem
    virtual LBItem::ptr get(uint64_t v = -1) override;

    FairLBItem::ptr getAsFair();
protected:
    virtual void initNolock();
private:
    //取dis = v % m_weights最后一个元素
    int32_t getIdx(uint64_t v = -1);
protected:
    std::vector<LBItem::ptr> m_items;
private:
    std::vector<int64_t> m_weights;
};

//利用每个流的权重和每个流多个时间点状态信息计算出的权重之积来随机选取一个流
class FairLB : public LB {
public:
    typedef std::shared_ptr<FairLB> ptr;
    //返回m_items中getIdx()后面第一个有效的LBItem
    virtual LBItem::ptr get() override;
    FairLBItem::ptr getAsFair();

protected:
    /**
     * @details 获取每一个FairLBItem的权重，然后将权重的前缀和存储在m_weights中,用来一句随机数按照权重取得对应流
    */
    virtual void initNolock();

private:
    //在[0, m_weights[-1]]范围内生成随机数，获得这个随机数的最大上届来获得流
    int32_t getIdx();
protected:
    std::vector<LBItem::ptr> m_items;
private:
    //weight的前缀和
    std::vector<int32_t> m_weights;
};

class SDLB {
public:
    typedef std::shared_ptr<SDLB> ptr;
    //通过ServiceItemInfo::ptr返回一个SockStream::ptr
    typedef std::function<SockStream::ptr(ServiceItemInfo::ptr)> stream_callback;
    typedef yhchaos::RWMtx RWMtxType;

    SDLB(ISD::ptr sd);

    virtual ~SDLB() {}

    //将onServiceChange设置为m_sd->(service_callback)m_cb,然后执行m_sd->start()
    virtual void start();
    //执行m_sd->stop()
    virtual void stop();

    stream_callback getCb() const { return m_cb;}
    void setCb(stream_callback v) { m_cb = v;}
    
    /**
     * @brief 获取domain-service负载均衡对象
     * @param[in] domain 域名
     * @param[in] service 服务名称
     * @param[in] auto_create 若不存在是否自动创建负载均衡对象
     * @return 返回m_datas中domain-service对应的负载均衡对象，auto_create=true时，若不存在则创建一个getType(domain, service)的负载均衡对象并添加到m_datas中
    */
    LB::ptr get(const std::string& domain, const std::string& service, bool auto_create = false);
    
    /**
     * @brief 重新初始化m_types和m_sd->m_queryInfos，初始化负载均衡类型的时候的同时初始化m_sd->m_queryInfos
     * @param[in] confs domain -> [service -> |"round_robin"|"weight"|"fair"|]
     * @details 根据confs创建m_types和m_sd->m_queryInfos对象，然后替换m_types和m_sd->m_queryInfos
    */
    void initConf(const std::unordered_map<std::string, 
    std::unordered_map<std::string, std::string> >& confs);
    
    /**
     * @brief 获取m_datas的状态信息
    */
    std::string statusString();
private:
    /**
     * @brief 当m_sd->m_datas[domain][service]发生变化时，调用该函数,调整相应的负载均衡对象
     * @param[in] domain 域名
     * @param[in] service 服务名称
     * @param[in] old_value 旧的{ServiceItemInfo::m_id->ServiceItemInfo::ptr}散列表
     * @param[in] new_value 新的{ServiceItemInfo::m_id->ServiceItemInfo::ptr}散列表
    */
    void onServiceChange(const std::string& domain, const std::string& service
                ,const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& old_value
                ,const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& new_value);
    
    /**
     * @brief 获取domain-service的负载均衡类型
     * @param[in] domain 域名
     * @param[in] service 服务名称
     * @details 返回m_types[domain][service]，如果m_types[domain][service]不存在，那么返回m_defaultType
    */
    ILB::Type getType(const std::string& domain, const std::string& service);
    
    /**
     * @brief 根据ILB::Type获取负载均衡对象
     * @param[in] type ROUNDROBIN/WEIGHT/FAIR
     * @return RoundRobinLB/WeightLB/FairLB
    */
    LB::ptr createLB(ILB::Type type);

    /**
     * @brief 根据ILB::Type返回对应的LBItem
     * @param[in] type ROUNDROBIN/WEIGHT/FAIR
     * @return LBItem/FairLBItem
    */
    LBItem::ptr createLBItem(ILB::Type type);

protected:
    RWMtxType m_mutex;
    ISD::ptr m_sd;//=sd
    //每一个domain-service都有一个负载均衡对象，表示该domain-service下的服务器如何进行负载均衡
    std::unordered_map<std::string, std::unordered_map<std::string, LB::ptr> > m_datas;
    //每一个domain-service都有一个负载均衡类型，表示该domain-service下的服务器选择什么样的负载均衡类型来进行负载均衡
    std::unordered_map<std::string, std::unordered_map<std::string, ILB::Type> > m_types;
    ILB::Type m_defaultType = ILB::FAIR;
    //获取某个ServiceItemInfo对应的流SockStream
    stream_callback m_cb;
};

}

#endif
