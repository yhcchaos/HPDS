#include "status_cpp_servlet.h"
#include "yhchaos/yhchaos.h"

namespace yhchaos {
namespace http {

StatusCppServlet::StatusCppServlet()
    :CppServlet("StatusCppServlet") {
}

std::string format_used_time(int64_t ts) {
    std::stringstream ss;
    bool v = false;
    if(ts >= 3600 * 24) {
        ss << (ts / 3600 / 24) << "d ";
        ts = ts % (3600 * 24);
        v = true;
    }
    if(ts >= 3600) {
        ss << (ts / 3600) << "h ";
        ts = ts % 3600;
        v = true;
    } else if(v) {
        ss << "0h ";
    }

    if(ts >= 60) {
        ss << (ts / 60) << "m ";
        ts = ts % 60;
    } else if(v) {
        ss << "0m ";
    }
    ss << ts << "s";
    return ss.str();
}

int32_t StatusCppServlet::handle(yhchaos::http::HttpReq::ptr request
                              ,yhchaos::http::HttpRsp::ptr response
                              ,yhchaos::http::HSession::ptr session) {
    response->setHeader("Content-Type", "text/text; charset=utf-8");
#define XX(key) \
    ss << std::setw(30) << std::right << key ": "
    std::stringstream ss;
    ss << "===================================================" << std::endl;
    XX("server_version") << "yhchaos/1.0.0" << std::endl;
    
    std::vector<Modularity::ptr> ms;
    ModularityMgr::GetInstance()->listAll(ms);

    XX("modules");
    for(size_t i = 0; i < ms.size(); ++i) {
        if(i) {
            ss << ";";
        }
        ss << ms[i]->getId();
    }
    ss << std::endl;
    XX("host") << GetHostName() << std::endl;
    XX("ipv4") << GetIPv4() << std::endl;
    XX("daemon_id") << ProcessInfoMgr::GetInstance()->parent_id << std::endl;
    XX("main_id") << ProcessInfoMgr::GetInstance()->main_id << std::endl;
    XX("daemon_start") << Time2Str(ProcessInfoMgr::GetInstance()->parent_start_time) << std::endl;
    XX("main_start") << Time2Str(ProcessInfoMgr::GetInstance()->main_start_time) << std::endl;
    XX("restart_count") << ProcessInfoMgr::GetInstance()->restart_count << std::endl;
    XX("daemon_running_time") << format_used_time(time(0) - ProcessInfoMgr::GetInstance()->parent_start_time) << std::endl;
    XX("main_running_time") << format_used_time(time(0) - ProcessInfoMgr::GetInstance()->main_start_time) << std::endl;
    ss << "===================================================" << std::endl;
    XX("coroutines") << yhchaos::Coroutine::TotalCoroutines() << std::endl;
    ss << "===================================================" << std::endl;
    ss << "<Logger>" << std::endl;
    ss << yhchaos::LoggerMgr::GetInstance()->toYamlString() << std::endl;
    ss << "===================================================" << std::endl;
    ss << "<Woker>" << std::endl;
    yhchaos::CoSchedulerMgr::GetInstance()->dump(ss) << std::endl;

    std::map<std::string, std::vector<TcpSvr::ptr> > servers;
    yhchaos::AppCase::GetInstance()->listAllSvr(servers);
    ss << "===================================================" << std::endl;
    for(auto it = servers.begin();
            it != servers.end(); ++it) {
        if(it != servers.begin()) {
            ss << "***************************************************" << std::endl;
        }
        ss << "<Svr." << it->first << ">" << std::endl;
        yhchaos::http::HttpSvr::ptr hs;
        for(auto iit = it->second.begin();
                iit != it->second.end(); ++iit) {
            if(iit != it->second.begin()) {
                ss << "---------------------------------------------------" << std::endl;
            }
            if(!hs) {
                hs = std::dynamic_pointer_cast<yhchaos::http::HttpSvr>(*iit);
            }
            ss << (*iit)->toString() << std::endl;
        }
        if(hs) {
            auto sd = hs->getCppServletDispatch();
            if(sd) {
                std::map<std::string, ICppServletCreator::ptr> infos;
                sd->listAllCppServletCreator(infos);
                if(!infos.empty()) {
                    ss << "[CppServlets]" << std::endl;
#define XX2(key) \
    ss << std::setw(30) << std::right << key << ": "
                    for(auto& i : infos) {
                        XX2(i.first) << i.second->getName() << std::endl;
                    }
                    infos.clear();
                }
                sd->listAllGlobCppServletCreator(infos);
                if(!infos.empty()) {
                    ss << "[CppServlets.Globs]" << std::endl;
                    for(auto& i : infos) {
                        XX2(i.first) << i.second->getName() << std::endl;
                    }
                    infos.clear();
                }
            }
        }
    }
    ss << "===================================================" << std::endl;
    for(size_t i = 0; i < ms.size(); ++i) {
        if(i) {
            ss << "***************************************************" << std::endl;
        }
        ss << ms[i]->statusString() << std::endl;
    }

    response->setBody(ss.str());
    return 0;
}

}
}
