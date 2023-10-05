#include "yhchaos/tcpserver.h"
#include "yhchaos/iocoscheduler.h"
#include "yhchaos/log.h"

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void run() {
    auto addr = yhchaos::NetworkAddress::SearchForAny("0.0.0.0:8033");
    //auto addr2 = yhchaos::UnixNetworkAddress::ptr(new yhchaos::UnixNetworkAddress("/tmp/unix_addr"));
    std::vector<yhchaos::NetworkAddress::ptr> addrs;
    addrs.push_back(addr);
    //addrs.push_back(addr2);

    yhchaos::TcpSvr::ptr tcp_server(new yhchaos::TcpSvr);
    std::vector<yhchaos::NetworkAddress::ptr> fails;
    while(!tcp_server->bind(addrs, fails)) {
        sleep(2);
    }
    tcp_server->start();
    
}
int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom(2);
    iom.coschedule(run);
    return 0;
}
