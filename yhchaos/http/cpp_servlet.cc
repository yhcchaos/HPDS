#include "cpp_servlet.h"
#include <fnmatch.h>

namespace yhchaos {
namespace http {

FunctionCppServlet::FunctionCppServlet(callback cb)
    :CppServlet("FunctionCppServlet")
    ,m_cb(cb) {
}

int32_t FunctionCppServlet::handle(yhchaos::http::HttpReq::ptr request
               , yhchaos::http::HttpRsp::ptr response
               , yhchaos::http::HSession::ptr session) {
    return m_cb(request, response, session);
}



CppServletDispatch::CppServletDispatch()
    :CppServlet("CppServletDispatch") {
    m_default.reset(new NotFoundCppServlet("yhchaos/1.0"));
}

int32_t CppServletDispatch::handle(yhchaos::http::HttpReq::ptr request
               , yhchaos::http::HttpRsp::ptr response
               , yhchaos::http::HSession::ptr session) {
    auto slt = getMatchedCppServlet(request->getPath());
    if(slt) {
        slt->handle(request, response, session);
    }
    return 0;
}

void CppServletDispatch::addCppServlet(const std::string& uri, CppServlet::ptr slt) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas[uri] = std::make_shared<HoldCppServletCreator>(slt);
}

void CppServletDispatch::addCppServletCreator(const std::string& uri, ICppServletCreator::ptr creator) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas[uri] = creator;
}

void CppServletDispatch::addGlobCppServletCreator(const std::string& uri, ICppServletCreator::ptr creator) {
    RWMtxType::WriteLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
    m_globs.push_back(std::make_pair(uri, creator));
}

void CppServletDispatch::addCppServlet(const std::string& uri
                        ,FunctionCppServlet::callback cb) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas[uri] = std::make_shared<HoldCppServletCreator>(
                        std::make_shared<FunctionCppServlet>(cb));
}

void CppServletDispatch::addGlobCppServlet(const std::string& uri
                                    ,CppServlet::ptr slt) {
    RWMtxType::WriteLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
    m_globs.push_back(std::make_pair(uri
                , std::make_shared<HoldCppServletCreator>(slt)));
}

void CppServletDispatch::addGlobCppServlet(const std::string& uri
                                ,FunctionCppServlet::callback cb) {
    return addGlobCppServlet(uri, std::make_shared<FunctionCppServlet>(cb));
}

void CppServletDispatch::delCppServlet(const std::string& uri) {
    RWMtxType::WriteLock lock(m_mutex);
    m_datas.erase(uri);
}

void CppServletDispatch::delGlobCppServlet(const std::string& uri) {
    RWMtxType::WriteLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
}

CppServlet::ptr CppServletDispatch::getCppServlet(const std::string& uri) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_datas.find(uri);
    return it == m_datas.end() ? nullptr : it->second->get();
}

CppServlet::ptr CppServletDispatch::getGlobCppServlet(const std::string& uri) {
    RWMtxType::ReadLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            return it->second->get();
        }
    }
    return nullptr;
}

CppServlet::ptr CppServletDispatch::getMatchedCppServlet(const std::string& uri) {
    RWMtxType::ReadLock lock(m_mutex);
    auto mit = m_datas.find(uri);
    if(mit != m_datas.end()) {
        return mit->second->get();
    }
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(!fnmatch(it->first.c_str(), uri.c_str(), 0)) {
            return it->second->get();
        }
    }
    return m_default;
}

void CppServletDispatch::listAllCppServletCreator(std::map<std::string, ICppServletCreator::ptr>& infos) {
    RWMtxType::ReadLock lock(m_mutex);
    for(auto& i : m_datas) {
        infos[i.first] = i.second;
    }
}

void CppServletDispatch::listAllGlobCppServletCreator(std::map<std::string, ICppServletCreator::ptr>& infos) {
    RWMtxType::ReadLock lock(m_mutex);
    for(auto& i : m_globs) {
        infos[i.first] = i.second;
    }
}

NotFoundCppServlet::NotFoundCppServlet(const std::string& name)
    :CppServlet("NotFoundCppServlet")
    ,m_name(name) {
    m_content = "<html><head><title>404 Not Found"
        "</title></head><body><center><h1>404 Not Found</h1></center>"
        "<hr><center>" + name + "</center></body></html>";

}

int32_t NotFoundCppServlet::handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) {
    response->setStatus(yhchaos::http::HStatus::NOT_FOUND);
    response->setHeader("Svr", "yhchaos/1.0.0");
    response->setHeader("Content-Type", "text/html");
    response->setBody(m_content);
    return 0;
}


}
}
