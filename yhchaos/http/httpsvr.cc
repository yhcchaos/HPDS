#include "httpsvr.h"
#include "yhchaos/log.h"
#include "yhchaos/http/servlets/config_cpp_servlet.h"
#include "yhchaos/http/servlets/status_cpp_servlet.h"

namespace yhchaos {
namespace http {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

HttpSvr::HttpSvr(bool keepalive
               ,yhchaos::IOCoScheduler* worker
               ,yhchaos::IOCoScheduler* io_worker
               ,yhchaos::IOCoScheduler* accept_worker)
    :TcpSvr(worker, io_worker, accept_worker)
    ,m_isKeepalive(keepalive) {
    m_dispatch.reset(new CppServletDispatch);

    m_type = "http";
    m_dispatch->addCppServlet("/_/status", CppServlet::ptr(new StatusCppServlet));
    m_dispatch->addCppServlet("/_/config", CppServlet::ptr(new AppConfigCppServlet));
}

void HttpSvr::setName(const std::string& v) {
    TcpSvr::setName(v);
    m_dispatch->setDefault(std::make_shared<NotFoundCppServlet>(v));
}
//httpReq.m_close和http_server的m_iskeepalive是为了在服务器端保持这个连接，即当收到
//已连接客户端的请求并发送回应后，继续在下一个循环中等待客户端发送的请求，而不是发送回应后
//就关闭socket和客户端的连接，退出handleClient协程。
void HttpSvr::handleClient(Sock::ptr client) {
    YHCHAOS_LOG_DEBUG(g_logger) << "handleClient " << *client;
    //因为一个新连接就是一个session
    HSession::ptr session(new HSession(client));
    do {
        auto req = session->recvReq();
        if(!req) {
            YHCHAOS_LOG_DEBUG(g_logger) << "recv http request fail, errno="
                << errno << " errstr=" << strerror(errno)
                << " cliet:" << *client << " keep_alive=" << m_isKeepalive;
            break;
        }

        HttpRsp::ptr rsp(new HttpRsp(req->getVersion()
                            ,req->isClose() || !m_isKeepalive));
        rsp->setHeader("Svr", getName());
        m_dispatch->handle(req, rsp, session);
        session->sendRsp(rsp);

        if(!m_isKeepalive || req->isClose()) {
            break;
        }
    } while(true);
    session->close();
}

}
}
