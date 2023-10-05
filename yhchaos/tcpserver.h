#ifndef __YHCHAOS_TCPSERVER_H__
#define __YHCHAOS_TCPSERVER_H__

#include <memory>
#include <functional>
#include "network_address.h"
#include "iocoscheduler.h"
#include "sock.h"
#include "noncopyable.h"
#include "appconfig.h"

namespace yhchaos {

struct TcpSvrConf {
    typedef std::shared_ptr<TcpSvrConf> ptr;

    std::vector<std::string> address;
    int keepalive = 0;
    int timeout = 1000 * 2 * 60;
    int ssl = 0;//是否使用ssl socket
    std::string id;
    /// 服务器类型，http, ws, dp
    std::string type = "http";
    std::string name;
    std::string cert_file;
    std::string key_file;
    std::string accept_worker;
    std::string io_worker;
    std::string process_worker;
    std::map<std::string, std::string> args;

    bool isValid() const {
        return !address.empty();
    }

    bool operator==(const TcpSvrConf& oth) const {
        return address == oth.address
            && keepalive == oth.keepalive
            && timeout == oth.timeout
            && name == oth.name
            && ssl == oth.ssl
            && cert_file == oth.cert_file
            && key_file == oth.key_file
            && accept_worker == oth.accept_worker
            && io_worker == oth.io_worker
            && process_worker == oth.process_worker
            && args == oth.args
            && id == oth.id
            && type == oth.type;
    }
};

template<>
class LexicalCast<std::string, TcpSvrConf> {
public:
    TcpSvrConf operator()(const std::string& v) {
        YAML::Node node = YAML::Resolve(v);
        TcpSvrConf conf;
        conf.id = node["id"].as<std::string>(conf.id);
        conf.type = node["type"].as<std::string>(conf.type);
        conf.keepalive = node["keepalive"].as<int>(conf.keepalive);
        conf.timeout = node["timeout"].as<int>(conf.timeout);
        conf.name = node["name"].as<std::string>(conf.name);
        conf.ssl = node["ssl"].as<int>(conf.ssl);
        conf.cert_file = node["cert_file"].as<std::string>(conf.cert_file);
        conf.key_file = node["key_file"].as<std::string>(conf.key_file);
        conf.accept_worker = node["accept_worker"].as<std::string>();
        conf.io_worker = node["io_worker"].as<std::string>();
        conf.process_worker = node["process_worker"].as<std::string>();
        conf.args = LexicalCast<std::string
            ,std::map<std::string, std::string> >()(node["args"].as<std::string>(""));
        if(node["address"].IsDefined()) {
            for(size_t i = 0; i < node["address"].size(); ++i) {
                conf.address.push_back(node["address"][i].as<std::string>());
            }
        }
        return conf;
    }
};

template<>
class LexicalCast<TcpSvrConf, std::string> {
public:
    std::string operator()(const TcpSvrConf& conf) {
        YAML::Node node;
        node["id"] = conf.id;
        node["type"] = conf.type;
        node["name"] = conf.name;
        node["keepalive"] = conf.keepalive;
        node["timeout"] = conf.timeout;
        node["ssl"] = conf.ssl;
        node["cert_file"] = conf.cert_file;
        node["key_file"] = conf.key_file;
        node["accept_worker"] = conf.accept_worker;
        node["io_worker"] = conf.io_worker;
        node["process_worker"] = conf.process_worker;
        node["args"] = YAML::Resolve(LexicalCast<std::map<std::string, std::string>
            , std::string>()(conf.args));
        for(auto& i : conf.address) {
            node["address"].push_back(i);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief TCP服务器封装
 */
class TcpSvr : public std::enable_shared_from_this<TcpSvr>
                    , Noncopyable {
public:
    typedef std::shared_ptr<TcpSvr> ptr;
    /**
     * @brief 构造函数
     * @param[in] worker socket客户端工作的协程调度器
     * @param[in] accept_worker 服务器socket执行接收socket连接的协程调度器
     */
    TcpSvr(yhchaos::IOCoScheduler* worker = yhchaos::IOCoScheduler::GetThis()
              ,yhchaos::IOCoScheduler* io_woker = yhchaos::IOCoScheduler::GetThis()
              ,yhchaos::IOCoScheduler* accept_worker = yhchaos::IOCoScheduler::GetThis());

    /**
     * @brief 析构函数
     * @details 关闭所有的监听套接字
     */
    virtual ~TcpSvr();

    /**
     * @brief 绑定地址
     * @return 返回是否绑定成功
     */
    virtual bool bind(yhchaos::NetworkAddress::ptr addr, bool ssl = false);

    /**
     * @brief 绑定地址数组,bind+listen, 有一个绑定或listen不成功就返回false，并清空m_socks
     * @param[in] addrs 需要绑定的地址数组
     * @param[out] fails 绑定失败的地址
     * @return 是否绑定成功
     */
    virtual bool bind(const std::vector<NetworkAddress::ptr>& addrs
                        ,std::vector<NetworkAddress::ptr>& fails
                        ,bool ssl = false);

    
    bool loadCertificates(const std::string& cert_file, const std::string& key_file);

    /**
     * @brief 启动服务
     * @pre 需要bind成功后执行
     * @details 对于每个监听套接字，把start_accept函数扔到m_acceptWorker线程池中进行调度
     */
    virtual bool start();

    /**
     * @brief 停止服务
     */
    virtual void stop();

    /**
     * @brief 返回读取超时时间(毫秒)
     */
    uint64_t getRecvTimeout() const { return m_recvTimeout;}

    /**
     * @brief 返回服务器名称
     */
    std::string getName() const { return m_name;}

    /**
     * @brief 设置读取超时时间(毫秒)
     */
    void setRecvTimeout(uint64_t v) { m_recvTimeout = v;}

    /**
     * @brief 设置服务器名称
     */
    virtual void setName(const std::string& v) { m_name = v;}

    /**
     * @brief 是否停止
     */
    bool isStop() const { return m_isStop;}

    TcpSvrConf::ptr getConf() const { return m_conf;}
    void setConf(TcpSvrConf::ptr v) { m_conf = v;}
    void setConf(const TcpSvrConf& v);

    virtual std::string toString(const std::string& prefix = "");

    std::vector<Sock::ptr> getSocks() const { return m_socks;}
protected:

    /**
     * @brief 处理新连接的Sock类
     */
    virtual void handleClient(Sock::ptr client);

    /**
     * @brief 开始接受连接，m_acceptWorker线程池中的协程会在收到新连接后，会产生一个新连接，
     * 把handleClient协程，扔到m_worker中去调度
     */
    virtual void startAccept(Sock::ptr sock);
protected:
    /// 监听Sock数组
    std::vector<Sock::ptr> m_socks;
    IOCoScheduler* m_worker;
    /// accept后产生的新连接Sock工作的调度器（线程池）
    IOCoScheduler* m_ioWorker;
    /// 服务器Sock接收连接的线程池
    IOCoScheduler* m_acceptWorker;
    /// accept返回新连接的接收超时时间(毫秒)，很久没发消息，防止恶意连接并占用资源
    uint64_t m_recvTimeout;
    /// 服务器名称，做日志的时候可以进行区分不同的server
    std::string m_name;
    /// 服务器类型
    std::string m_type = "tcp";
    /// server是否停止，start中设置为false，stop中设置为true
    bool m_isStop;

    bool m_ssl = false;
    //tcp_server的配置文件
    TcpSvrConf::ptr m_conf;
};

}

#endif
