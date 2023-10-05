#ifndef __YHCHAOS_HTTP_WS_SERVER_H__
#define __YHCHAOS_HTTP_WS_SERVER_H__

#include "yhchaos/tcpserver.h"
#include "ws_session.h"
#include "ws_servlet.h"

namespace yhchaos {
namespace http {

class WSvr : public TcpSvr {
public:
    typedef std::shared_ptr<WSvr> ptr;

    WSvr(yhchaos::IOCoScheduler* worker = yhchaos::IOCoScheduler::GetThis()
             , yhchaos::IOCoScheduler* io_worker = yhchaos::IOCoScheduler::GetThis()
             , yhchaos::IOCoScheduler* accept_worker = yhchaos::IOCoScheduler::GetThis());

    WCppServletDispatch::ptr getWCppServletDispatch() const { return m_dispatch;}
    void setWCppServletDispatch(WCppServletDispatch::ptr v) { m_dispatch = v;}
protected:
    virtual void handleClient(Sock::ptr client) override;
protected:
    WCppServletDispatch::ptr m_dispatch;
};

}
}

#endif
