#include "modularity.h"
#include "appconfig.h"
#include "environment.h"
#include "library.h"
#include "util.h"
#include "log.h"
#include "appcase.h"

namespace yhchaos {

static yhchaos::AppConfigVar<std::string>::ptr g_module_path
    = AppConfig::SearchFor("module.path", std::string("module"), "module path");

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

Modularity::Modularity(const std::string& name
            ,const std::string& version
            ,const std::string& filename
            ,uint32_t type)
    :m_name(name)
    ,m_version(version)
    ,m_filename(filename)
    ,m_id(name + "/" + version)
    ,m_type(type) {
}

void Modularity::onBeforeArgsParse(int argc, char** argv) {
}

void Modularity::onAfterArgsParse(int argc, char** argv) {
}

bool Modularity::handleReq(yhchaos::MSG::ptr req
                           ,yhchaos::MSG::ptr rsp
                           ,yhchaos::Stream::ptr stream) {
    YHCHAOS_LOG_DEBUG(g_logger) << "handleReq req=" << req->toString()
            << " rsp=" << rsp->toString() << " stream=" << stream;
    return true;
}

bool Modularity::handleNotify(yhchaos::MSG::ptr notify
                          ,yhchaos::Stream::ptr stream) {
    YHCHAOS_LOG_DEBUG(g_logger) << "handleNotify nty=" << notify->toString()
            << " stream=" << stream;
    return true;
}

bool Modularity::onResolve() {
    return true;
}

bool Modularity::onUnload() {
    return true;
}

bool Modularity::onConnect(yhchaos::Stream::ptr stream) {
    return true;
}

bool Modularity::onDisconnect(yhchaos::Stream::ptr stream) {
    return true;
}

bool Modularity::onSvrReady() {
    return true;
}

bool Modularity::onSvrUp() {
    return true;
}

void Modularity::registerService(const std::string& server_type,
            const std::string& domain, const std::string& service) {
    auto sd = AppCase::GetInstance()->getSD();
    if(!sd) {
        return;
    }
    std::vector<TcpSvr::ptr> svrs;
    if(!AppCase::GetInstance()->getSvr(server_type, svrs)) {
        return;
    }
    for(auto& i : svrs) {
        auto socks = i->getSocks();
        for(auto& s : socks) {
            auto addr = std::dynamic_pointer_cast<IPv4NetworkAddress>(s->getLocalNetworkAddress());
            if(!addr) {
                continue;
            }
            auto str = addr->toString();
            if(str.find("127.0.0.1") == 0) {
                continue;
            }
            std::string ip_and_port;
            if(str.find("0.0.0.0") == 0) {
                ip_and_port = yhchaos::GetIPv4() + ":" + std::to_string(addr->getPort());
            } else {
                ip_and_port = addr->toString();
            }
            sd->registerSvr(domain, service, ip_and_port, server_type);
        }
    }
}

std::string Modularity::statusString() {
    std::stringstream ss;
    ss << "Modularity name=" << getName()
       << " version=" << getVersion()
       << " filename=" << getFilename()
       << std::endl;
    return ss.str();
}

DPModularity::DPModularity(const std::string& name
                       ,const std::string& version
                       ,const std::string& filename)
    :Modularity(name, version, filename, DP) {
}

bool DPModularity::handleReq(yhchaos::MSG::ptr req
                               ,yhchaos::MSG::ptr rsp
                               ,yhchaos::Stream::ptr stream) {
    auto dp_req = std::dynamic_pointer_cast<yhchaos::DPReq>(req);
    auto dp_rsp = std::dynamic_pointer_cast<yhchaos::DPRsp>(rsp);
    auto dp_stream = std::dynamic_pointer_cast<yhchaos::DPStream>(stream);
    return handleDPReq(dp_req, dp_rsp, dp_stream);
}

bool DPModularity::handleNotify(yhchaos::MSG::ptr notify
                              ,yhchaos::Stream::ptr stream) {
    auto dp_nty = std::dynamic_pointer_cast<yhchaos::DPNotify>(notify);
    auto dp_stream = std::dynamic_pointer_cast<yhchaos::DPStream>(stream);
    return handleDPNotify(dp_nty, dp_stream);
}

ModularityManager::ModularityManager() {
}

Modularity::ptr ModularityManager::get(const std::string& name) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_modules.find(name);
    return it == m_modules.end() ? nullptr : it->second;
}


void ModularityManager::add(Modularity::ptr m) {
    del(m->getId());
    RWMtxType::WriteLock lock(m_mutex);
    m_modules[m->getId()] = m;
    m_type2Modularitys[m->getType()][m->getId()] = m;
}

void ModularityManager::del(const std::string& name) {
    Modularity::ptr module;
    RWMtxType::WriteLock lock(m_mutex);
    auto it = m_modules.find(name);
    if(it == m_modules.end()) {
        return;
    }
    module = it->second;
    m_modules.erase(it);
    m_type2Modularitys[module->getType()].erase(module->getId());
    if(m_type2Modularitys[module->getType()].empty()) {
        m_type2Modularitys.erase(module->getType());
    }
    lock.unlock();
    module->onUnload();
}

void ModularityManager::delAll() {
    RWMtxType::ReadLock lock(m_mutex);
    auto tmp = m_modules;
    lock.unlock();

    for(auto& i : tmp) {
        del(i.first);
    }
}

void ModularityManager::init() {
    //module path
    auto path = EnvironmentMgr::GetInstance()->getAbsolutePath(g_module_path->getValue());
    //获取所有的module文件路径
    std::vector<std::string> files;
    yhchaos::FSUtil::ListAllFile(files, path, ".so");

    std::sort(files.begin(), files.end());
    /**
     * Modularity::ptr m = Library::GetModularity(path);
        if(m) {
            add(m);
    }
    */
    for(auto& i : files) {
        initModularity(i);
    }
}

void ModularityManager::listByType(uint32_t type, std::vector<Modularity::ptr>& ms) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_type2Modularitys.find(type);
    if(it == m_type2Modularitys.end()) {
        return;
    }
    for(auto& i : it->second) {
        ms.push_back(i.second);
    }
}

void ModularityManager::foreach(uint32_t type, std::function<void(Modularity::ptr)> cb) {
    std::vector<Modularity::ptr> ms;
    listByType(type, ms);
    for(auto& i : ms) {
        cb(i);
    }
}

void ModularityManager::onConnect(Stream::ptr stream) {
    std::vector<Modularity::ptr> ms;
    listAll(ms);

    for(auto& m : ms) {
        m->onConnect(stream);
    }
}

void ModularityManager::onDisconnect(Stream::ptr stream) {
    std::vector<Modularity::ptr> ms;
    listAll(ms);

    for(auto& m : ms) {
        m->onDisconnect(stream);
    }
}

void ModularityManager::listAll(std::vector<Modularity::ptr>& ms) {
    RWMtxType::ReadLock lock(m_mutex);
    for(auto& i : m_modules) {
        ms.push_back(i.second);
    }
}

void ModularityManager::initModularity(const std::string& path) {
    Modularity::ptr m = Library::GetModularity(path);
    if(m) {
        add(m);
    }
}

}
