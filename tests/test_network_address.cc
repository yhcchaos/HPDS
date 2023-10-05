#include "yhchaos/network_address.h"
#include "yhchaos/log.h"

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void test() {
    std::vector<yhchaos::NetworkAddress::ptr> addrs;

    YHCHAOS_LOG_INFO(g_logger) << "begin";
    bool v = yhchaos::NetworkAddress::SearchFor(addrs, "localhost:3080");
    //bool v = yhchaos::NetworkAddress::SearchFor(addrs, "www.baidu.com", AF_INET);
    //bool v = yhchaos::NetworkAddress::SearchFor(addrs, "www.yhchaos.top", AF_INET);
    YHCHAOS_LOG_INFO(g_logger) << "end";
    if(!v) {
        YHCHAOS_LOG_ERROR(g_logger) << "lookup fail";
        return;
    }

    for(size_t i = 0; i < addrs.size(); ++i) {
        YHCHAOS_LOG_INFO(g_logger) << i << " - " << addrs[i]->toString();
    }

    auto addr = yhchaos::NetworkAddress::SearchForAny("localhost:4080");
    if(addr) {
        YHCHAOS_LOG_INFO(g_logger) << *addr;
    } else {
        YHCHAOS_LOG_ERROR(g_logger) << "error";
    }
}

void test_iface() {
    std::multimap<std::string, std::pair<yhchaos::NetworkAddress::ptr, uint32_t> > results;

    bool v = yhchaos::NetworkAddress::GetInterfaceNetworkAddresses(results);
    if(!v) {
        YHCHAOS_LOG_ERROR(g_logger) << "GetInterfaceNetworkAddresses fail";
        return;
    }

    for(auto& i: results) {
        YHCHAOS_LOG_INFO(g_logger) << i.first << " - " << i.second.first->toString() << " - "
            << i.second.second;
    }
}

void test_ipv4() {
    //auto addr = yhchaos::IPNetworkAddress::Create("www.yhchaos.top");
    auto addr = yhchaos::IPNetworkAddress::Create("127.0.0.8");
    if(addr) {
        YHCHAOS_LOG_INFO(g_logger) << addr->toString();
    }
}

int main(int argc, char** argv) {
    //test_ipv4();
    //test_iface();
    test();
    return 0;
}
