#include "yhchaos/http/httpsvr.h"
#include "yhchaos/log.h"

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();
yhchaos::IOCoScheduler::ptr worker;
void run() {
    g_logger->setLevel(yhchaos::LogLevel::INFO);
    yhchaos::NetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress("0.0.0.0:8020");
    if(!addr) {
        YHCHAOS_LOG_ERROR(g_logger) << "get address error";
        return;
    }

    yhchaos::http::HttpSvr::ptr http_server(new yhchaos::http::HttpSvr(true, worker.get()));
    //yhchaos::http::HttpSvr::ptr http_server(new yhchaos::http::HttpSvr(true));
    bool ssl = false;
    while(!http_server->bind(addr, ssl)) {
        YHCHAOS_LOG_ERROR(g_logger) << "bind " << *addr << " fail";
        sleep(1);
    }

    if(ssl) {
        //http_server->loadCertificates("/home/apps/soft/yhchaos/keys/server.crt", "/home/apps/soft/yhchaos/keys/server.key");
    }

    http_server->start();
}

int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom(1);
    worker.reset(new yhchaos::IOCoScheduler(4, false));
    iom.coschedule(run);
    return 0;
}
