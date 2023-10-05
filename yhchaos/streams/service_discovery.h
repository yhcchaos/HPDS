#ifndef __YHCHAOS_STREAMS_SERVICE_DISCOVERY_H__
#define __YHCHAOS_STREAMS_SERVICE_DISCOVERY_H__

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include "yhchaos/mtx.h"
#include "yhchaos/iocoscheduler.h"
#include "yhchaos/zk_cli.h"

namespace yhchaos {
/**
 * @brief 保存并向zk服务器注册服务信息
 * @details 
 *  1. 创建zk客户端m_client，然后在m_timer中创建一个60s后启动的onZKConnect定时任务
 *  2. 设置并保存provider和consumer服务信息，包括ip和port，data
 *  3. 向zk服务器m_hosts，注册provider和consumer服务信息，并执行provider服务信息改变之后的回调函数
*/
class ServiceItemInfo {
public:
    typedef std::shared_ptr<ServiceItemInfo> ptr;
    static ServiceItemInfo::ptr Create(const std::string& ip_and_port, const std::string& data);

    uint64_t getId() const { return m_id;}
    uint16_t getPort() const { return m_port;}
    const std::string& getIp() const { return m_ip;}
    const std::string& getData() const { return m_data;}

    std::string toString() const;
private:
    uint64_t m_id;
    uint16_t m_port;
    std::string m_ip;
    std::string m_data;
};
//生成的默认构造函数
class ISD {
public:
    typedef std::shared_ptr<ISD> ptr;
    typedef std::function<void(const std::string& domain, const std::string& service
                ,const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& old_value
                ,const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& new_value)> service_callback;
    virtual ~ISD() { }

    void registerSvr(const std::string& domain, const std::string& service,
                        const std::string& ip_and_port, const std::string& data);
    void querySvr(const std::string& domain, const std::string& service);

    void listSvr(std::unordered_map<std::string, std::unordered_map<std::string
                    ,std::unordered_map<uint64_t, ServiceItemInfo::ptr> > >& infos);
    void listRegisterSvr(std::unordered_map<std::string, std::unordered_map<std::string
                            ,std::unordered_map<std::string, std::string> > >& infos);

    void listQuerySvr(std::unordered_map<std::string, std::unordered_set<std::string> >& infos);

    virtual void start() = 0;
    virtual void stop() = 0;

    service_callback getServiceCallback() const { return m_cb;}
    void setServiceCallback(service_callback v) { m_cb = v;}
    //m_queryInfos = v
    void setQuerySvr(const std::unordered_map<std::string, std::unordered_set<std::string> >& v);
protected:
    yhchaos::RWMtx m_mutex;
    //客户端会用到，当前感兴趣的开放域名：服务的所有可用主机
    std::unordered_map<std::string, std::unordered_map<std::string
        ,std::unordered_map<uint64_t, ServiceItemInfo::ptr> > > m_datas;
    //provider, 提供服务的服务器端会设置这个成员，当服务开放时会将服务信息(ip+port)注册到zkserver
    std::unordered_map<std::string, std::unordered_map<std::string
        ,std::unordered_map<std::string, std::string> > > m_registerInfos;
    //注册consumer, domain -> [service]，感兴趣的域名和服务名，客户端会根据这个信息向zkserver查询某域名和服务名已注册的所有主机，保存在m_datas中
    std::unordered_map<std::string, std::unordered_set<std::string> > m_queryInfos;
    //当m_datas[domain][service]发生变化时，调用m_cb(domain, service, old_infos, infos)
    service_callback m_cb;
};

class ZKSD : public ISD
                          ,public std::enable_shared_from_this<ZKSD> {
public:
    typedef std::shared_ptr<ZKSD> ptr;
    ZKSD(const std::string& hosts);
    const std::string& getSelfInfo() const { return m_selfInfo;}
    void setSelfInfo(const std::string& v) { m_selfInfo = v;}
    const std::string& getSelfData() const { return m_selfData;}
    void setSelfData(const std::string& v) { m_selfData = v;}

    /**
     * @details 
     *  1. 初始化m_client，然后在m_timer中创建一个60s后启动的onZKConnect定时任务
     *  2. 定时任务启动时，设置m_isOnTimedCoroutine=true，调用onZKConnect向zk_mnode注册服务信息
     *  3. 定时任务结束后，设置m_isOnTimedCoroutine=false
    */  
    virtual void start();

    /**
     * @details 断开m_client连接，从m_timer是删除定时任务
    */
    virtual void stop();
private:
    /**
     * @details m_client的watcher回调函数，根据type和stat的值，调用不同的回调函数
    */
    void onWatch(int type, int stat, const std::string& path, ZKCli::ptr);

    /**
     * @brief 向mnode注册provider和consumer服务信息，然后依据新注册的provider跟新m_datas[domain][service]散列表
     * @param[in] path 节点路径
     * @param[in] client zk客户端
     *  
    */
    void onZKConnect(const std::string& path, ZKCli::ptr client);
    void onZKChild(const std::string& path, ZKCli::ptr client);
    void onZKChanged(const std::string& path, ZKCli::ptr client);
    void onZKDeleted(const std::string& path, ZKCli::ptr client);
    void onZKExpiredSession(const std::string& path, ZKCli::ptr client);

    /**
     * @brief 开放服务的服务器向zkserver注册临时provider服务节点(m_client.create)，不设置监听器
     * @details 创建的临时子节点，/yhchaos/domain/service/providers/ip_and_port, value=data
     * @param[in] domain 域名，构建节点路径
     * @param[in] service 服务名，构建节点路径
     * @param[in] ip_and_port ip和端口，构建节点路径
     * @param[in] data 数据
    */
    bool registerInfo(const std::string& domain, const std::string& service, 
                      const std::string& ip_and_port, const std::string& data);

    /**
     * @brief 注册consumers服务信息(m_client.create()),主要是m_queryInfos中的元素
     * @param[in] domain 域名，构建节点路径
     * @param[in] service 服务名，构建节点路径
     */ 
    bool queryInfo(const std::string& domain, const std::string& service);

    /**
     * @brief 获取domain和service中的所有provider（ip_and_port)
     * @param[in] domain 域名，构建节点路径
     * @param[in] service 服务名，构建节点路径
     */
    bool queryData(const std::string& domain, const std::string& service);

    bool existsOrCreate(const std::string& path);

    /**
     * @brief 根据path所有子节点，更新m_datas[domain][service]散列表（伴随着service_callback的调用）
     * @param[in] path 父节点路径，path=/yhchaos/domain/service/provider
    */
    bool getChildren(const std::string& path);
private:
    //逗号分隔的 host:port 对列表，每个对应于一个 zookeeper 服务器
    std::string m_hosts;
    std::string m_selfInfo;
    std::string m_selfData;
    ZKCli::ptr m_client;
    yhchaos::TimedCoroutine::ptr m_timer;
    bool m_isOnTimedCoroutine = false;
};

}

#endif
