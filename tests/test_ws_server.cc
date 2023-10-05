#include "yhchaos/http/ws_server.h"
#include "yhchaos/log.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void run() {
    yhchaos::http::WSvr::ptr server(new yhchaos::http::WSvr);
    yhchaos::NetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress("0.0.0.0:8020");
    if(!addr) {
        YHCHAOS_LOG_ERROR(g_logger) << "get address error";
        return;
    }
    auto fun = [](yhchaos::http::HttpReq::ptr header
                  ,yhchaos::http::WFrameMSG::ptr msg
                  ,yhchaos::http::WSession::ptr session) {
        session->sendMSG(msg);
        return 0;
    };

    server->getWCppServletDispatch()->addCppServlet("/yhchaos", fun);
    while(!server->bind(addr)) {
        YHCHAOS_LOG_ERROR(g_logger) << "bind " << *addr << " fail";
        sleep(1);
    }
    server->start();
}

int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom(2);
    iom.coschedule(run);
    return 0;
}
