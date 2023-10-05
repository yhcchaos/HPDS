#ifndef __YHCHAOS_HTTP_WS_SERVLET_H__
#define __YHCHAOS_HTTP_WS_SERVLET_H__

#include "ws_session.h"
#include "yhchaos/cpp_thread.h"
#include "cpp_servlet.h"

namespace yhchaos {
namespace http {

class WCppServlet : public CppServlet {
public:
    typedef std::shared_ptr<WCppServlet> ptr;
    WCppServlet(const std::string& name)
        :CppServlet(name) {
    }
    virtual ~WCppServlet() {}

    virtual int32_t handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) override {
        return 0;
    }

    virtual int32_t onConnect(yhchaos::http::HttpReq::ptr header
                              ,yhchaos::http::WSession::ptr session) = 0;
    virtual int32_t onClose(yhchaos::http::HttpReq::ptr header
                             ,yhchaos::http::WSession::ptr session) = 0;
    virtual int32_t handle(yhchaos::http::HttpReq::ptr header
                           ,yhchaos::http::WFrameMSG::ptr msg
                           ,yhchaos::http::WSession::ptr session) = 0;
    const std::string& getName() const { return m_name;}
protected:
    std::string m_name;
};

class FunctionWCppServlet : public WCppServlet {
public:
    typedef std::shared_ptr<FunctionWCppServlet> ptr;
    typedef std::function<int32_t (yhchaos::http::HttpReq::ptr header
                              ,yhchaos::http::WSession::ptr session)> on_connect_cb;
    typedef std::function<int32_t (yhchaos::http::HttpReq::ptr header
                             ,yhchaos::http::WSession::ptr session)> on_close_cb; 
    typedef std::function<int32_t (yhchaos::http::HttpReq::ptr header
                           ,yhchaos::http::WFrameMSG::ptr msg
                           ,yhchaos::http::WSession::ptr session)> callback;

    FunctionWCppServlet(callback cb
                      ,on_connect_cb connect_cb = nullptr
                      ,on_close_cb close_cb = nullptr);

    virtual int32_t onConnect(yhchaos::http::HttpReq::ptr header
                              ,yhchaos::http::WSession::ptr session) override;
    virtual int32_t onClose(yhchaos::http::HttpReq::ptr header
                             ,yhchaos::http::WSession::ptr session) override;
    virtual int32_t handle(yhchaos::http::HttpReq::ptr header
                           ,yhchaos::http::WFrameMSG::ptr msg
                           ,yhchaos::http::WSession::ptr session) override;
protected:
    callback m_callback;
    on_connect_cb m_onConnect;
    on_close_cb m_onClose;
};

class WCppServletDispatch : public CppServletDispatch {
public:
    typedef std::shared_ptr<WCppServletDispatch> ptr;
    typedef RWMtx RWMtxType;

    WCppServletDispatch();
    void addCppServlet(const std::string& uri
                    ,FunctionWCppServlet::callback cb
                    ,FunctionWCppServlet::on_connect_cb connect_cb = nullptr
                    ,FunctionWCppServlet::on_close_cb close_cb = nullptr);
    void addGlobCppServlet(const std::string& uri
                    ,FunctionWCppServlet::callback cb
                    ,FunctionWCppServlet::on_connect_cb connect_cb = nullptr
                    ,FunctionWCppServlet::on_close_cb close_cb = nullptr);
    WCppServlet::ptr getWCppServlet(const std::string& uri);
};

}
}

#endif
