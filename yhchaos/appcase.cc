#include "appcase.h"

#include <unistd.h>
#include <signal.h>

#include "yhchaos/tcpserver.h"
#include "yhchaos/daemon.h"
#include "yhchaos/appconfig.h"
#include "yhchaos/environment.h"
#include "yhchaos/log.h"
#include "yhchaos/modularity.h"
#include "yhchaos/dp/dp_stream.h"
#include "yhchaos/worker.h"
#include "yhchaos/http/ws_server.h"
#include "yhchaos/dp/dp_server.h"
#include "yhchaos/db/watch_thread.h"
#include "yhchaos/db/cpp_redis.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

static yhchaos::AppConfigVar<std::string>::ptr g_server_work_path =
    yhchaos::AppConfig::SearchFor("server.work_path"
            ,std::string("/apps/work/yhchaos")
            , "server work path");

static yhchaos::AppConfigVar<std::string>::ptr g_server_pid_file =
    yhchaos::AppConfig::SearchFor("server.pid_file"
            ,std::string("yhchaos.pid")
            , "server pid file");

static yhchaos::AppConfigVar<std::string>::ptr g_service_discovery_zk =
    yhchaos::AppConfig::SearchFor("service_discovery.zk"
            ,std::string("")
            , "service discovery zookeeper");


static yhchaos::AppConfigVar<std::vector<TcpSvrConf> >::ptr g_servers_conf
    = yhchaos::AppConfig::SearchFor("servers", std::vector<TcpSvrConf>(), "http server config");

AppCase* AppCase::s_instance = nullptr;

AppCase::AppCase() {
    s_instance = this;
}

bool AppCase::init(int argc, char** argv) {
    m_argc = argc;
    m_argv = argv;

    yhchaos::EnvironmentMgr::GetInstance()->addHelp("s", "start with the terminal");
    yhchaos::EnvironmentMgr::GetInstance()->addHelp("d", "run as daemon");
    yhchaos::EnvironmentMgr::GetInstance()->addHelp("c", "conf path default: ./conf");//E:\yhchaos\template\bin\conf
    yhchaos::EnvironmentMgr::GetInstance()->addHelp("p", "print help");

    bool is_print_help = false;
    if(!yhchaos::EnvironmentMgr::GetInstance()->init(argc, argv)) {
        is_print_help = true;
    }

    if(yhchaos::EnvironmentMgr::GetInstance()->has("p")) {
        is_print_help = true;
    }

    std::string conf_path = yhchaos::EnvironmentMgr::GetInstance()->getAppConfigPath();
    YHCHAOS_LOG_INFO(g_logger) << "load conf path:" << conf_path;
    yhchaos::AppConfig::ResolveFromConfDir(conf_path);

    ModularityMgr::GetInstance()->init();
    std::vector<Modularity::ptr> modules;
    ModularityMgr::GetInstance()->listAll(modules);

    for(auto i : modules) {
        i->onBeforeArgsParse(argc, argv);
    }

    if(is_print_help) {
        yhchaos::EnvironmentMgr::GetInstance()->printHelp();
        return false;
    }

    for(auto i : modules) {
        i->onAfterArgsParse(argc, argv);
    }
    modules.clear();

    int run_type = 0;
    if(yhchaos::EnvironmentMgr::GetInstance()->has("s")) {
        run_type = 1;//"start with the terminal"
    }
    if(yhchaos::EnvironmentMgr::GetInstance()->has("d")) {
        run_type = 2;//"run as daemon"
    }

    if(run_type == 0) {
        yhchaos::EnvironmentMgr::GetInstance()->printHelp();
        return false;
    }

    std::string pidfile = g_server_work_path->getValue()
                                + "/" + g_server_pid_file->getValue();
    if(yhchaos::FSUtil::IsRunningPidfile(pidfile)) {
        YHCHAOS_LOG_ERROR(g_logger) << "server is running:" << pidfile;
        return false;
    }

    if(!yhchaos::FSUtil::Mkdir(g_server_work_path->getValue())) {
        YHCHAOS_LOG_FATAL(g_logger) << "create work path [" << g_server_work_path->getValue()
            << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool AppCase::run() {
    bool is_daemon = yhchaos::EnvironmentMgr::GetInstance()->has("d");
    return start_daemon(m_argc, m_argv,
            std::bind(&AppCase::main, this, std::placeholders::_1,
                std::placeholders::_2), is_daemon);
}

int AppCase::main(int argc, char** argv) {\
    signal(SIGPIPE, SIG_IGN);
    YHCHAOS_LOG_INFO(g_logger) << "main";
    std::string conf_path = yhchaos::EnvironmentMgr::GetInstance()->getAppConfigPath();
    yhchaos::AppConfig::ResolveFromConfDir(conf_path, true);
    {
        std::string pidfile = g_server_work_path->getValue()
                                    + "/" + g_server_pid_file->getValue();
        /**
         * std::ofstream ofs(pidfile); 代码会尝试创建一个名为 pidfile 的文件输出流。
         * 如果 pidfile 不存在，它会尝试创建这个文件。
        */
        std::ofstream ofs(pidfile);
        if(!ofs) {
            YHCHAOS_LOG_ERROR(g_logger) << "open pidfile " << pidfile << " failed";
            return false;
        }
        ofs << getpid();
    }

    m_mainIOCoScheduler.reset(new yhchaos::IOCoScheduler(1, true, "main"));
    m_mainIOCoScheduler->coschedule(std::bind(&AppCase::run_coroutine, this));
    m_mainIOCoScheduler->addTimedCoroutine(2000, [](){
            //YHCHAOS_LOG_INFO(g_logger) << "hello";
    }, true);
    //在stop函数里面会执行m_rootFIber->call()，从而该线程从主协程切换到根协程执行
    m_mainIOCoScheduler->stop();
    return 0;
}

int AppCase::run_coroutine() {
    std::vector<Modularity::ptr> modules;
    ModularityMgr::GetInstance()->listAll(modules);
    bool has_error = false;
    for(auto& i : modules) {
        if(!i->onResolve()) {//module.onload***
            YHCHAOS_LOG_ERROR(g_logger) << "module name="
                << i->getName() << " version=" << i->getVersion()
                << " filename=" << i->getFilename();
            has_error = true;
        }
    }
    if(has_error) {
        _exit(0);
    }
    //配置文件中加载worker_config=[name->[thread_num/work_num:num]]，创建worker_manager,里面保存worker io_coscheduler
    //两个调度器：{"io（名字）":{thread_num:4}（线程数量）,"accept":{thread_num:1}}
    yhchaos::CoSchedulerMgr::GetInstance()->init();
    //一个foxthread {redis(name):foxCppThreadPoll(num=2)}
    WatchCppThreadMgr::GetInstance()->init();
    //创建了两个额外线程去执行函数
    WatchCppThreadMgr::GetInstance()->start();
    CppRedisMgr::GetInstance();

    auto http_confs = g_servers_conf->getValue();//std::vector<TcpSvrConf>
    std::vector<TcpSvr::ptr> svrs;
    for(auto& i : http_confs) {
        YHCHAOS_LOG_DEBUG(g_logger) << std::endl << LexicalCast<TcpSvrConf, std::string>()(i);

        std::vector<NetworkAddress::ptr> address;
        for(auto& a : i.address) {
            size_t pos = a.find(":");
            if(pos == std::string::npos) {
                //YHCHAOS_LOG_ERROR(g_logger) << "invalid address: " << a;
                address.push_back(UnixNetworkAddress::ptr(new UnixNetworkAddress(a)));
                continue;
            }
            int32_t port = atoi(a.substr(pos + 1).c_str());
            //127.0.0.1
            auto addr = yhchaos::IPNetworkAddress::Create(a.substr(0, pos).c_str(), port);
            if(addr) {
                address.push_back(addr);
                continue;
            }
            std::vector<std::pair<NetworkAddress::ptr, uint32_t> > result;
            //网卡上所有的ipvv4地址
            if(yhchaos::NetworkAddress::GetInterfaceNetworkAddresses(result,
                                        a.substr(0, pos))) {
                for(auto& x : result) {
                    auto ipaddr = std::dynamic_pointer_cast<IPNetworkAddress>(x.first);
                    if(ipaddr) {
                        ipaddr->setPort(atoi(a.substr(pos + 1).c_str()));
                    }
                    address.push_back(ipaddr);
                }
                continue;
            }

            auto aaddr = yhchaos::NetworkAddress::SearchForAny(a);
            if(aaddr) {
                address.push_back(aaddr);
                continue;
            }
            YHCHAOS_LOG_ERROR(g_logger) << "invalid address: " << a;
            _exit(0);
        }
        IOCoScheduler* accept_worker = yhchaos::IOCoScheduler::GetThis();
        IOCoScheduler* io_worker = yhchaos::IOCoScheduler::GetThis();
        IOCoScheduler* process_worker = yhchaos::IOCoScheduler::GetThis();
        if(!i.accept_worker.empty()) {
            accept_worker = yhchaos::CoSchedulerMgr::GetInstance()->getAsIOCoScheduler(i.accept_worker).get();
            if(!accept_worker) {
                YHCHAOS_LOG_ERROR(g_logger) << "accept_worker: " << i.accept_worker
                    << " not exists";
                _exit(0);
            }
        }
        if(!i.io_worker.empty()) {
            io_worker = yhchaos::CoSchedulerMgr::GetInstance()->getAsIOCoScheduler(i.io_worker).get();
            if(!io_worker) {
                YHCHAOS_LOG_ERROR(g_logger) << "io_worker: " << i.io_worker
                    << " not exists";
                _exit(0);
            }
        }
        if(!i.process_worker.empty()) {
            process_worker = yhchaos::CoSchedulerMgr::GetInstance()->getAsIOCoScheduler(i.process_worker).get();
            if(!process_worker) {
                YHCHAOS_LOG_ERROR(g_logger) << "process_worker: " << i.process_worker
                    << " not exists";
                _exit(0);
            }
        }

        TcpSvr::ptr server;
        if(i.type == "http") {
            server.reset(new yhchaos::http::HttpSvr(i.keepalive,
                            process_worker, io_worker, accept_worker));
        } else if(i.type == "ws") {
            server.reset(new yhchaos::http::WSvr(
                            process_worker, io_worker, accept_worker));
        } else if(i.type == "dp") {
            server.reset(new yhchaos::DPSvr("dp",
                            process_worker, io_worker, accept_worker));
        } else if(i.type == "nameserver") {
            server.reset(new yhchaos::DPSvr("nameserver",
                            process_worker, io_worker, accept_worker));
            ModularityMgr::GetInstance()->add(std::make_shared<yhchaos::ns::NameSvrModularity>());
        } else {
            YHCHAOS_LOG_ERROR(g_logger) << "invalid server type=" << i.type
                << LexicalCast<TcpSvrConf, std::string>()(i);
            _exit(0);
        }
        if(!i.name.empty()) {
            server->setName(i.name);
        }
        std::vector<NetworkAddress::ptr> fails;
        if(!server->bind(address, fails, i.ssl)) {
            for(auto& x : fails) {
                YHCHAOS_LOG_ERROR(g_logger) << "bind address fail:"
                    << *x;
            }
            _exit(0);
        }
        if(i.ssl) {
            if(!server->loadCertificates(i.cert_file, i.key_file)) {
                YHCHAOS_LOG_ERROR(g_logger) << "loadCertificates fail, cert_file="
                    << i.cert_file << " key_file=" << i.key_file;
            }
        }
        server->setConf(i);
        //server->start();
        m_servers[i.type].push_back(server);
        svrs.push_back(server);
    }

    if(!g_service_discovery_zk->getValue().empty()) {
        m_serviceDiscovery.reset(new ZKSD(g_service_discovery_zk->getValue()));
        m_dpSDLoadBalance.reset(new DPSDLoadBalance(m_serviceDiscovery));

        std::vector<TcpSvr::ptr> svrs;
        if(!getSvr("http", svrs)) {
            m_serviceDiscovery->setSelfInfo(yhchaos::GetIPv4() + ":0:" + yhchaos::GetHostName());
        } else {
            std::string ip_and_port;
            for(auto& i : svrs) {
                auto socks = i->getSocks();
                for(auto& s : socks) {
                    //服务器绑定的本地ip地址
                    auto addr = std::dynamic_pointer_cast<IPv4NetworkAddress>(s->getLocalNetworkAddress());
                    if(!addr) {
                        continue;
                    }
                    auto str = addr->toString();
                    if(str.find("127.0.0.1") == 0) {
                        continue;
                    }
                    if(str.find("0.0.0.0") == 0) {
                        ip_and_port = yhchaos::GetIPv4() + ":" + std::to_string(addr->getPort());
                        break;
                    } else {
                        ip_and_port = addr->toString();
                    }
                }
                if(!ip_and_port.empty()) {
                    break;
                }
            }
            m_serviceDiscovery->setSelfInfo(ip_and_port + ":" + yhchaos::GetHostName());
        }
    }

    for(auto& i : modules) {
        i->onSvrReady();
    }

    for(auto& i : svrs) {
        i->start();
    }

    if(m_dpSDLoadBalance) {
        m_dpSDLoadBalance->start();
    }

    for(auto& i : modules) {
        i->onSvrUp();
    }
    /*
    ZKSD::ptr m_serviceDiscovery;
    DPSDLoadBalance::ptr m_dpSDLoadBalance;
    yhchaos::ZKSD::ptr zksd(new yhchaos::ZKSD("127.0.0.1:21811"));
    zksd->registerSvr("blog", "chat", yhchaos::GetIPv4() + ":8090", "xxx");
    zksd->querySvr("blog", "chat");
    zksd->setSelfInfo(yhchaos::GetIPv4() + ":8090");
    zksd->setSelfData("vvv");
    static DPSDLoadBalance::ptr rsdlb(new DPSDLoadBalance(zksd));
    rsdlb->start();
    */
    return 0;
}

bool AppCase::getSvr(const std::string& type, std::vector<TcpSvr::ptr>& svrs) {
    auto it = m_servers.find(type);
    if(it == m_servers.end()) {
        return false;
    }
    svrs = it->second;
    return true;
}

void AppCase::listAllSvr(std::map<std::string, std::vector<TcpSvr::ptr> >& servers) {
    servers = m_servers;
}

}
