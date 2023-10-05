#include "yhchaos/sock.h"
#include "yhchaos/yhchaos.h"
#include "yhchaos/iocoscheduler.h"

static yhchaos::Logger::ptr g_looger = YHCHAOS_LOG_ROOT();

void test_socket() {
    //std::vector<yhchaos::NetworkAddress::ptr> addrs;
    //yhchaos::NetworkAddress::SearchFor(addrs, "www.baidu.com", AF_INET);
    //yhchaos::IPNetworkAddress::ptr addr;
    //for(auto& i : addrs) {
    //    YHCHAOS_LOG_INFO(g_looger) << i->toString();
    //    addr = std::dynamic_pointer_cast<yhchaos::IPNetworkAddress>(i);
    //    if(addr) {
    //        break;
    //    }
    //}
    yhchaos::IPNetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress("www.baidu.com");
    if(addr) {
        YHCHAOS_LOG_INFO(g_looger) << "get address: " << addr->toString();
    } else {
        YHCHAOS_LOG_ERROR(g_looger) << "get address fail";
        return;
    }

    yhchaos::Sock::ptr sock = yhchaos::Sock::CreateTCP(addr);
    addr->setPort(80);
    YHCHAOS_LOG_INFO(g_looger) << "addr=" << addr->toString();
    if(!sock->connect(addr)) {
        YHCHAOS_LOG_ERROR(g_looger) << "connect " << addr->toString() << " fail";
        return;
    } else {
        YHCHAOS_LOG_INFO(g_looger) << "connect " << addr->toString() << " connected";
    }

    const char buff[] = "GET / HTTP/1.0\r\n\r\n";
    int rt = sock->send(buff, sizeof(buff));
    if(rt <= 0) {
        YHCHAOS_LOG_INFO(g_looger) << "send fail rt=" << rt;
        return;
    }

    std::string buffs;
    buffs.resize(4096);
    rt = sock->recv(&buffs[0], buffs.size());

    if(rt <= 0) {
        YHCHAOS_LOG_INFO(g_looger) << "recv fail rt=" << rt;
        return;
    }

    buffs.resize(rt);
    YHCHAOS_LOG_INFO(g_looger) << buffs;
}

void test2() {
    yhchaos::IPNetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress("www.baidu.com:80");
    if(addr) {
        YHCHAOS_LOG_INFO(g_looger) << "get address: " << addr->toString();
    } else {
        YHCHAOS_LOG_ERROR(g_looger) << "get address fail";
        return;
    }

    yhchaos::Sock::ptr sock = yhchaos::Sock::CreateTCP(addr);
    if(!sock->connect(addr)) {
        YHCHAOS_LOG_ERROR(g_looger) << "connect " << addr->toString() << " fail";
        return;
    } else {
        YHCHAOS_LOG_INFO(g_looger) << "connect " << addr->toString() << " connected";
    }

    uint64_t ts = yhchaos::GetCurrentUS();
    for(size_t i = 0; i < 10000000000ul; ++i) {
        if(int err = sock->getError()) {
            YHCHAOS_LOG_INFO(g_looger) << "err=" << err << " errstr=" << strerror(err);
            break;
        }

        //struct tcp_info tcp_info;
        //if(!sock->getOption(IPPROTO_TCP, TCP_INFO, tcp_info)) {
        //    YHCHAOS_LOG_INFO(g_looger) << "err";
        //    break;
        //}
        //if(tcp_info.tcpi_state != TCP_ESTABLISHED) {
        //    YHCHAOS_LOG_INFO(g_looger)
        //            << " state=" << (int)tcp_info.tcpi_state;
        //    break;
        //}
        static int batch = 10000000;
        if(i && (i % batch) == 0) {
            uint64_t ts2 = yhchaos::GetCurrentUS();
            YHCHAOS_LOG_INFO(g_looger) << "i=" << i << " used: " << ((ts2 - ts) * 1.0 / batch) << " us";
            ts = ts2;
        }
    }
}

int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom;
    //iom.coschedule(&test_socket);
    iom.coschedule(&test2);
    return 0;
}
