#ifndef __YHCHAOS_APPCASE_H__
#define __YHCHAOS_APPCASE_H__

#include "yhchaos/http/httpsvr.h"
#include "yhchaos/streams/service_discovery.h"
#include "yhchaos/dp/dp_stream.h"

namespace yhchaos {

class AppCase {
public:
    AppCase();

    static AppCase* GetInstance() { return s_instance;}
    //加载配置文件，初始化参数模块env，初始化module模块，
    bool init(int argc, char** argv);
    bool run();

    bool getSvr(const std::string& type, std::vector<TcpSvr::ptr>& svrs);
    void listAllSvr(std::map<std::string, std::vector<TcpSvr::ptr> >& servers);

    ZKSD::ptr getSD() const { return m_serviceDiscovery;}
    DPSDLoadBalance::ptr getDPSDLoadBalance() const { return m_dpSDLoadBalance;}
private:
    int main(int argc, char** argv);
    int run_coroutine();
private:
    int m_argc = 0;//init中赋值=argc
    char** m_argv = nullptr;//init中赋值=argv

    //std::vector<yhchaos::http::HttpSvr::ptr> m_httpservers;
    std::map<std::string, std::vector<TcpSvr::ptr> > m_servers;
    IOCoScheduler::ptr m_mainIOCoScheduler;
    static AppCase* s_instance;//this

    //分布式服务发现模块
    ZKSD::ptr m_serviceDiscovery;
    //负载均衡模块
    DPSDLoadBalance::ptr m_dpSDLoadBalance;
};

}

#endif
