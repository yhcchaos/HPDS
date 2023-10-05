#include "ws_cpp_servlet.h"

namespace yhchaos {
namespace http {

FunctionWCppServlet::FunctionWCppServlet(callback cb
                                     ,on_connect_cb connect_cb
                                     ,on_close_cb close_cb)
    :WCppServlet("FunctionWCppServlet")
    ,m_callback(cb)
    ,m_onConnect(connect_cb)
    ,m_onClose(close_cb) {
}

int32_t FunctionWCppServlet::onConnect(yhchaos::http::HttpReq::ptr header
                                     ,yhchaos::http::WSession::ptr session) {
    if(m_onConnect) {
        return m_onConnect(header, session);
    }
    return 0;
}

int32_t FunctionWCppServlet::onClose(yhchaos::http::HttpReq::ptr header
                                     ,yhchaos::http::WSession::ptr session) {
    if(m_onClose) {
        return m_onClose(header, session);
    }
    return 0;
}

int32_t FunctionWCppServlet::handle(yhchaos::http::HttpReq::ptr header
                                   ,yhchaos::http::WFrameMSG::ptr msg
                                   ,yhchaos::http::WSession::ptr session) {
    if(m_callback) {
        return m_callback(header, msg, session);
    }
    return 0;
}

WCppServletDispatch::WCppServletDispatch() {
    m_name = "WCppServletDispatch";
}

void WCppServletDispatch::addCppServlet(const std::string& uri
                    ,FunctionWCppServlet::callback cb
                    ,FunctionWCppServlet::on_connect_cb connect_cb
                    ,FunctionWCppServlet::on_close_cb close_cb) {
    CppServletDispatch::addCppServlet(uri, std::make_shared<FunctionWCppServlet>(cb, connect_cb, close_cb));
}

void WCppServletDispatch::addGlobCppServlet(const std::string& uri
                    ,FunctionWCppServlet::callback cb
                    ,FunctionWCppServlet::on_connect_cb connect_cb
                    ,FunctionWCppServlet::on_close_cb close_cb) {
    CppServletDispatch::addGlobCppServlet(uri, std::make_shared<FunctionWCppServlet>(cb, connect_cb, close_cb));
}

WCppServlet::ptr WCppServletDispatch::getWCppServlet(const std::string& uri) {
    auto slt = getMatchedCppServlet(uri);
    return std::dynamic_pointer_cast<WCppServlet>(slt);
}

}
}
