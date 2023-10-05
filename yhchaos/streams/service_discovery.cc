#include "service_discovery.h"
#include "yhchaos/log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

ServiceItemInfo::ptr ServiceItemInfo::Create(const std::string& ip_and_port, const std::string& data) {
    auto pos = ip_and_port.find(':');
    if(pos == std::string::npos) {
        return nullptr;
    }
    auto ip = ip_and_port.substr(0, pos);
    auto port = yhchaos::TypeUtil::Atoi(ip_and_port.substr(pos + 1));
    in_addr_t ip_addr = inet_addr(ip.c_str());
    if(ip_addr == 0) {
        return nullptr;
    }

    ServiceItemInfo::ptr rt(new ServiceItemInfo);
    rt->m_id = ((uint64_t)ip_addr << 32) | port;
    rt->m_ip = ip;
    rt->m_port = port;
    rt->m_data = data;
    return rt;
}

std::string ServiceItemInfo::toString() const {
    std::stringstream ss;
    ss << "[ServiceItemInfo id=" << m_id
       << " ip=" << m_ip
       << " port=" << m_port
       << " data=" << m_data
       << "]";
    return ss.str();
}

void ISD::setQuerySvr(const std::unordered_map<std::string, std::unordered_set<std::string> >& v) {
    yhchaos::RWMtx::WriteLock lock(m_mutex);
    m_queryInfos = v;
}

void ISD::registerSvr(const std::string& domain, const std::string& service,
                                       const std::string& ip_and_port, const std::string& data) {
    yhchaos::RWMtx::WriteLock lock(m_mutex);
    m_registerInfos[domain][service][ip_and_port] = data;
}

void ISD::querySvr(const std::string& domain, const std::string& service) {
    yhchaos::RWMtx::WriteLock lock(m_mutex);
    m_queryInfos[domain].insert(service);
}

void ISD::listSvr(std::unordered_map<std::string, std::unordered_map<std::string
                                   ,std::unordered_map<uint64_t, ServiceItemInfo::ptr> > >& infos) {
    yhchaos::RWMtx::ReadLock lock(m_mutex);
    infos = m_datas;
}

void ISD::listRegisterSvr(std::unordered_map<std::string, std::unordered_map<std::string
                                           ,std::unordered_map<std::string, std::string> > >& infos) {
    yhchaos::RWMtx::ReadLock lock(m_mutex);
    infos = m_registerInfos;
}

void ISD::listQuerySvr(std::unordered_map<std::string
                                        ,std::unordered_set<std::string> >& infos) {
    yhchaos::RWMtx::ReadLock lock(m_mutex);
    infos = m_queryInfos;
}

ZKSD::ZKSD(const std::string& hosts)
    :m_hosts(hosts) {
}

void ZKSD::start() {
    if(m_client) {
        return;
    }
    auto self = shared_from_this();
    m_client.reset(new yhchaos::ZKCli);
    bool b = m_client->init(m_hosts, 6000, std::bind(&ZKSD::onWatch,
                self, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4));
    if(!b) {
        YHCHAOS_LOG_ERROR(g_logger) << "ZKCli init fail, hosts=" << m_hosts;
    }
    //每60秒更新一次m_datas，看感兴趣的域名：服务主机是否变动
    m_timer = yhchaos::IOCoScheduler::GetThis()->addTimedCoroutine(60 * 1000, [self, this](){
        m_isOnTimedCoroutine = true;
        onZKConnect("", m_client);
        m_isOnTimedCoroutine = false;
    }, true);
}

void ZKSD::stop() {
    if(m_client) {
        m_client->close();
        m_client = nullptr;
    }
    if(m_timer) {
        m_timer->cancel();
        m_timer = nullptr;
    }
}

void ZKSD::onZKConnect(const std::string& path, ZKCli::ptr client) {
    yhchaos::RWMtx::ReadLock lock(m_mutex);
    auto rinfo = m_registerInfos;
    auto qinfo = m_queryInfos;
    lock.unlock();

    bool ok = true;
    //注册m_registerInfos[domain][service][ip_and_port] = data
    //i=domain -> [service -> [ip_and_port -> data] ]
    for(auto& i : rinfo) {
        //x=service -> [ip_and_port -> data]
        for(auto& x : i.second) {
            //v=[ip_and_port -> data]
            for(auto& v : x.second) {
                ok &= registerInfo(i.first, x.first, v.first, v.second);
            }
        }
    }

    if(!ok) {
        YHCHAOS_LOG_ERROR(g_logger) << "onZKConnect register fail";
    }

    ok = true;
    //i=domain -> [service]
    for(auto& i : qinfo) {
        //x=service
        for(auto& x : i.second) {
            ok &= queryInfo(i.first, x);
        }
    }
    if(!ok) {
        YHCHAOS_LOG_ERROR(g_logger) << "onZKConnect query fail";
    }

    ok = true;
    //i=domain -> [service]
    for(auto& i : qinfo) {
        for(auto& x : i.second) {
             //x=service
            ok &= queryData(i.first, x);
        }
    }

    if(!ok) {
        YHCHAOS_LOG_ERROR(g_logger) << "onZKConnect queryData fail";
    }
}

bool ZKSD::existsOrCreate(const std::string& path) {
    int32_t v = m_client->exists(path, false);
    if(v == ZOK) {
        return true;
    } else {
        auto pos = path.find_last_of('/');
        if(pos == std::string::npos) {
            YHCHAOS_LOG_ERROR(g_logger) << "existsOrCreate invalid path=" << path;
            return false;
        }
        if(pos == 0 || existsOrCreate(path.substr(0, pos))) {
            std::string new_val(1024, 0);
            v = m_client->create(path, "", new_val);
            if(v != ZOK) {
                YHCHAOS_LOG_ERROR(g_logger) << "create path=" << path << " error:"
                    << zerror(v) << " (" << v << ")";
                return false;
            }
            return true;
        }
        //if(pos == 0) {
        //    std::string new_val(1024, 0);
        //    if(m_client->create(path, "", new_val) != ZOK) {
        //        return false;
        //    }
        //    return true;
        //}
    }
    return false;
}

static std::string GetProvidersPath(const std::string& domain, const std::string& service) {
    return "/yhchaos/" + domain + "/" + service + "/providers";
}

static std::string GetConsumersPath(const std::string& domain, const std::string& service) {
    return "/yhchaos/" + domain + "/" + service + "/consumers";
}

static std::string GetDomainPath(const std::string& domain) {
    return "/yhchaos/" + domain;
}

bool ParseDomainService(const std::string& path, std::string& domain, std::string& service) {
    auto v = yhchaos::split(path, '/');
    if(v.size() != 5) {
        return false;
    }
    domain = v[2];
    service = v[3];
    return true;
}

bool ZKSD::registerInfo(const std::string& domain, const std::string& service, 
                                      const std::string& ip_and_port, const std::string& data) {
    //"/yhchaos/" + domain + "/" + service + "/providers"
    std::string path = GetProvidersPath(domain, service);
    bool v = existsOrCreate(path);
    if(!v) {
        YHCHAOS_LOG_ERROR(g_logger) << "create path=" << path << " fail";
        return false;
    }

    std::string new_val(1024, 0);
    int32_t rt = m_client->create(path + "/" + ip_and_port, data, new_val
                                  ,&ZOO_OPEN_ACL_UNSAFE, ZKCli::FlagsType::EPHEMERAL);
    if(rt == ZOK) {
        return true;
    }
    if(!m_isOnTimedCoroutine) {
        YHCHAOS_LOG_ERROR(g_logger) << "create path=" << (path + "/" + ip_and_port) << " fail, error:"
            << zerror(rt) << " (" << rt << ")";
    }
    return rt == ZNODEEXISTS;
}

bool ZKSD::queryInfo(const std::string& domain, const std::string& service) {
    if(service != "all") {
        //path=/yhchaos/domain/service/consumers;
        std::string path = GetConsumersPath(domain, service);
        bool v = existsOrCreate(path);
        if(!v) {
            YHCHAOS_LOG_ERROR(g_logger) << "create path=" << path << " fail";
            return false;
        }

        if(m_selfInfo.empty()) {
            YHCHAOS_LOG_ERROR(g_logger) << "queryInfo selfInfo is null";
            return false;
        }

        std::string new_val(1024, 0);
        int32_t rt = m_client->create(path + "/" + m_selfInfo, m_selfData, new_val
                                      ,&ZOO_OPEN_ACL_UNSAFE, ZKCli::FlagsType::EPHEMERAL);
        if(rt == ZOK) {
            return true;
        }
        if(!m_isOnTimedCoroutine) {
            YHCHAOS_LOG_ERROR(g_logger) << "create path=" << (path + "/" + m_selfInfo) << " fail, error:"
                << zerror(rt) << " (" << rt << ")";
        }
        return rt == ZNODEEXISTS;
    } else {
        std::vector<std::string> children;
        //getChildren(string)返回/yhchaos/domain;子节点存储在children中
        m_client->getChildren(GetDomainPath(domain), children, false);
        bool rt = true;
        for(auto& i : children) {
            rt &= queryInfo(domain, i);
        }
        return rt;
    }
}

bool ZKSD::getChildren(const std::string& path) {
    std::string domain;
    std::string service;
    if(!ParseDomainService(path, domain, service)) {
        YHCHAOS_LOG_ERROR(g_logger) << "get_children path=" << path
            << " invalid path";
        return false;
    }
    {
        yhchaos::RWMtx::ReadLock lock(m_mutex);
        auto it = m_queryInfos.find(domain);
        if(it == m_queryInfos.end()) {
            YHCHAOS_LOG_ERROR(g_logger) << "get_children path=" << path
                << " domian=" << domain << " not exists";
            return false;
        }
        if(it->second.count(service) == 0
                && it->second.count("all") == 0) {
            YHCHAOS_LOG_ERROR(g_logger) << "get_children path=" << path
                << " service=" << service << " not exists "
                << yhchaos::Join(it->second.begin(), it->second.end(), ",");
            return false;
        }
    }

    std::vector<std::string> vals;
    int32_t v = m_client->getChildren(path, vals, true);
    if(v != ZOK) {
        YHCHAOS_LOG_ERROR(g_logger) << "get_children path=" << path << " fail, error:"
            << zerror(v) << " (" << v << ")";
        return false;
    }
    std::unordered_map<uint64_t, ServiceItemInfo::ptr> infos;
    for(auto& i : vals) {
        auto info = ServiceItemInfo::Create(i, "");
        if(!info) {
            continue;
        }
        infos[info->getId()] = info;
        YHCHAOS_LOG_INFO(g_logger) << "domain=" << domain
            << " service=" << service << " info=" << info->toString();
    }

    auto new_vals = infos;
    yhchaos::RWMtx::WriteLock lock(m_mutex);
    m_datas[domain][service].swap(infos);
    lock.unlock();

    m_cb(domain, service, infos, new_vals);
    return true;
}

bool ZKSD::queryData(const std::string& domain, const std::string& service) {
    //YHCHAOS_LOG_INFO(g_logger) << "query_data domain=" << domain
    //                         << " service=" << service;
    if(service != "all") {
        std::string path = GetProvidersPath(domain, service);
        return getChildren(path);
    } else {
        std::vector<std::string> children;
        m_client->getChildren(GetDomainPath(domain), children, false);
        bool rt = true;
        for(auto& i : children) {
            rt &= queryData(domain, i);
        }
        return rt;
    }
}

void ZKSD::onZKChild(const std::string& path, ZKCli::ptr client) {
    //YHCHAOS_LOG_INFO(g_logger) << "onZKChild path=" << path;
    getChildren(path);
}

void ZKSD::onZKChanged(const std::string& path, ZKCli::ptr client) {
    YHCHAOS_LOG_INFO(g_logger) << "onZKChanged path=" << path;
}

void ZKSD::onZKDeleted(const std::string& path, ZKCli::ptr client) {
    YHCHAOS_LOG_INFO(g_logger) << "onZKDeleted path=" << path;
}

void ZKSD::onZKExpiredSession(const std::string& path, ZKCli::ptr client) {
    YHCHAOS_LOG_INFO(g_logger) << "onZKExpiredSession path=" << path;
    client->reconnect();
}

void ZKSD::onWatch(int type, int stat, const std::string& path, ZKCli::ptr client) {
    //表示连接成功
    if(stat == ZKCli::StateType::CONNECTED) {
        if(type == ZKCli::FdEventType::SESSION) {
                return onZKConnect(path, client);
        } else if(type == ZKCli::FdEventType::CHILD) {
                return onZKChild(path, client);
        } else if(type == ZKCli::FdEventType::CHANGED) {
                return onZKChanged(path, client);
        } else if(type == ZKCli::FdEventType::DELETED) {
                return onZKDeleted(path, client);
        } 
    } else if(stat == ZKCli::StateType::EXPIRED_SESSION) {
        if(type == ZKCli::FdEventType::SESSION) {
            return onZKExpiredSession(path, client);
        }
    }
    YHCHAOS_LOG_ERROR(g_logger) << "onWatch hosts=" << m_hosts
        << " type=" << type << " stat=" << stat
        << " path=" << path << " client=" << client;
}

}
