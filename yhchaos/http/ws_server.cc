#include "ws_server.h"
#include "yhchaos/log.h"

namespace yhchaos {
namespace http {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

WSvr::WSvr(yhchaos::IOCoScheduler* worker, yhchaos::IOCoScheduler* io_worker, yhchaos::IOCoScheduler* accept_worker)
    :TcpSvr(worker, io_worker, accept_worker) {
    m_dispatch.reset(new WCppServletDispatch);
    m_type = "websocket_server";
}

void WSvr::handleClient(Sock::ptr client) {
    YHCHAOS_LOG_DEBUG(g_logger) << "handleClient " << *client;
    WSession::ptr session(new WSession(client));
    do {
        HttpReq::ptr header = session->handleShake();
        if(!header) {
            YHCHAOS_LOG_DEBUG(g_logger) << "handleShake error";
            break;
        }
        WCppServlet::ptr servlet = m_dispatch->getWCppServlet(header->getPath());
        if(!servlet) {
            YHCHAOS_LOG_DEBUG(g_logger) << "no match WCppServlet";
            break;
        }
        int rt = servlet->onConnect(header, session);
        if(rt) {
            YHCHAOS_LOG_DEBUG(g_logger) << "onConnect return " << rt;
            break;
        }
        while(true) {
            auto msg = session->recvMSG();
            if(!msg) {
                break;
            }
            rt = servlet->handle(header, msg, session);
            if(rt) {
                YHCHAOS_LOG_DEBUG(g_logger) << "handle return " << rt;
                break;
            }
        }
        servlet->onClose(header, session);
    } while(0);
    session->close();
}

}
}
