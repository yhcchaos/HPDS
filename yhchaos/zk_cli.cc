#include "zk_client.h"

namespace yhchaos {

//static const int CREATED = ZOO_CREATED_EVENT;
//static const int DELETED = ZOO_DELETED_EVENT;
//static const int CHANGED = ZOO_CHANGED_EVENT;
//static const int CHILD   = ZOO_CHILD_EVENT;
//static const int SESSION = ZOO_SESSION_EVENT;
//static const int NOWATCHING = ZOO_NOTWATCHING_EVENT;

const int ZKCli::FdEventType::CREATED = ZOO_CREATED_EVENT;
const int ZKCli::FdEventType::DELETED = ZOO_DELETED_EVENT;
const int ZKCli::FdEventType::CHANGED = ZOO_CHANGED_EVENT;
const int ZKCli::FdEventType::CHILD   = ZOO_CHILD_EVENT;
const int ZKCli::FdEventType::SESSION = ZOO_SESSION_EVENT;
const int ZKCli::FdEventType::NOWATCHING = ZOO_NOTWATCHING_EVENT;

const int ZKCli::FlagsType::EPHEMERAL = ZOO_EPHEMERAL;
const int ZKCli::FlagsType::SEQUENCE  = ZOO_SEQUENCE;
const int ZKCli::FlagsType::CONTAINER = ZOO_CONTAINER;

const int ZKCli::StateType::EXPIRED_SESSION = ZOO_EXPIRED_SESSION_STATE;
const int ZKCli::StateType::AUTH_FAILED = ZOO_AUTH_FAILED_STATE;
const int ZKCli::StateType::CONNECTING = ZOO_CONNECTING_STATE;
const int ZKCli::StateType::ASSOCIATING = ZOO_ASSOCIATING_STATE;
const int ZKCli::StateType::CONNECTED = ZOO_CONNECTED_STATE;
const int ZKCli::StateType::READONLY = ZOO_READONLY_STATE;
const int ZKCli::StateType::NOTCONNECTED = ZOO_NOTCONNECTED_STATE;


ZKCli::ZKCli()
    :m_handle(nullptr)
    ,m_recvTimeout(0) {
}

ZKCli::~ZKCli() {
    if(m_handle) {
        close();
    }
}
//是一个静态函数
void ZKCli::OnWatcher(zhandle_t *zh, int type, int stat, const char *path,void *watcherCtx) {
    ZKCli* client = (ZKCli*)watcherCtx;
    client->m_watcherCb(type, stat, path);
}

bool ZKCli::reconnect() {
    if(m_handle) {
        zookeeper_close(m_handle);
    }
    m_handle = zookeeper_init2(m_hosts.c_str(), &ZKCli::OnWatcher, m_recvTimeout, nullptr, this, 0, m_logCb);
    return m_handle != nullptr;
}

bool ZKCli::init(const std::string& hosts, int recv_timeout, watcher_callback cb, log_callback lcb) {
    if(m_handle) {
        return true;
    }
    m_hosts = hosts;
    m_recvTimeout = recv_timeout;
    
    m_watcherCb = std::bind(cb, std::placeholders::_1,
                            std::placeholders::_2,
                            std::placeholders::_3,
                            shared_from_this());
    m_logCb = lcb;
    m_handle = zookeeper_init2(hosts.c_str(), &ZKCli::OnWatcher, m_recvTimeout, nullptr, this, 0, lcb);
    return m_handle != nullptr;
}

int32_t ZKCli::setSvrs(const std::string& hosts) {
    auto rt = zoo_set_servers(m_handle, hosts.c_str());
    if(rt == 0) {
        m_hosts = hosts;
    }
    return rt;
}

int32_t ZKCli::create(const std::string& path, const std::string& val, std::string& new_path
                         ,const struct ACL_vector* acl
                         ,int flags) {
    return zoo_create(m_handle, path.c_str(), val.c_str(), val.size(), acl, flags, &new_path[0], new_path.size());
}

int32_t ZKCli::exists(const std::string& path, bool watch, Stat* stat) {
    return zoo_exists(m_handle, path.c_str(), watch, stat);
}

int32_t ZKCli::del(const std::string& path, int version) {
    return zoo_delete(m_handle, path.c_str(), version);
}

int32_t ZKCli::get(const std::string& path, std::string& val, bool watch, Stat* stat) {
    int len = val.size();
    int32_t rt = zoo_get(m_handle, path.c_str(), watch, &val[0], &len, stat);
    if(rt == ZOK) {
        val.resize(len);
    }
    return rt;
}

int32_t ZKCli::getAppConfig(std::string& val, bool watch, Stat* stat) {
//#define ZOO_CONFIG_NODE "/zookeeper/config"，在zookeeper.h中定义
    return get(ZOO_CONFIG_NODE, val, watch, stat);
}

int32_t ZKCli::set(const std::string& path, const std::string& val, int version, Stat* stat) {
    return zoo_set2(m_handle, path.c_str(), val.c_str(), val.size(), version, stat);
}

int32_t ZKCli::getChildren(const std::string& path, std::vector<std::string>& val, bool watch, Stat* stat) {
    String_vector strings;
    Stat tmp;
    if(stat == nullptr) {
        stat = &tmp;
    }
    int32_t rt = zoo_get_children2(m_handle, path.c_str(), watch, &strings, stat);
    if(rt == ZOK) {
        for(int32_t i = 0; i < strings.count; ++i) {
            val.push_back(strings.data[i]);
        }
        //使用 deallocate_String_vector(在generated/zookeeper.jute.h中)来释放内存,
        deallocate_String_vector(&strings);
    }
    return rt;
}

int32_t ZKCli::close() {
    m_watcherCb = nullptr;
    int32_t rt = ZOK;
    if(m_handle) {
        rt = zookeeper_close(m_handle);
        m_handle = nullptr;
    }
    return rt;
}

std::string  ZKCli::getCurrentSvr() {
    auto rt = zoo_get_current_server(m_handle);
    return rt == nullptr ? "" : rt;
}

int32_t ZKCli::getState() {
    return zoo_state(m_handle);
}

}
