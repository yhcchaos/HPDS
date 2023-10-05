#include "network_address.h"
#include "log.h"
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>

#include "endian.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
//?????
template<class T>
static T CreateMask(uint32_t bits) {
    return (1 << (sizeof(T) * 8 - bits)) - 1;
}

template<class T>
static uint32_t CountBytes(T value) {
    uint32_t result = 0;
    for(; value; ++result) {
        value &= value - 1;
    }
    return result;
}

NetworkAddress::ptr NetworkAddress::SearchForAny(const std::string& host,
                                int family, int type, int protocol) {
    std::vector<NetworkAddress::ptr> result;
    if(SearchFor(result, host, family, type, protocol)) {
        return result[0];
    }
    return nullptr;
}

IPNetworkAddress::ptr NetworkAddress::SearchForAnyIPNetworkAddress(const std::string& host,
                                int family, int type, int protocol) {
    std::vector<NetworkAddress::ptr> result;
    if(SearchFor(result, host, family, type, protocol)) {
        //for(auto& i : result) {
        //    std::cout << i->toString() << std::endl;
        //}
        for(auto& i : result) {
            IPNetworkAddress::ptr v = std::dynamic_pointer_cast<IPNetworkAddress>(i);
            if(v) {
                return v;
            }
        }
    }
    return nullptr;
}


bool NetworkAddress::SearchFor(std::vector<NetworkAddress::ptr>& result, const std::string& host,
                     int family, int type, int protocol) {
    addrinfo hints, *results, *next;
    hints.ai_flags = 0;
    hints.ai_family = family;
    hints.ai_socktype = type;
    hints.ai_protocol = protocol;
    hints.ai_addrlen = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    std::string node;
    const char* service = NULL;

    //检查 ipv6address serivce
    if(!host.empty() && host[0] == '[') {
        const char* endipv6 = (const char*)memchr(host.c_str() + 1, ']', host.size() - 1);
        if(endipv6) {
            //TODO check out of range
            if(*(endipv6 + 1) == ':') {
                service = endipv6 + 2;
            }
            node = host.substr(1, endipv6 - host.c_str() - 1);
        }
    }

    //检查 node serivce
    if(node.empty()) {
        service = (const char*)memchr(host.c_str(), ':', host.size());
        if(service) {
            if(!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) {
                node = host.substr(0, service - host.c_str());
                ++service;
            }
        }
    }

    if(node.empty()) {
        node = host;
    }
    int error = getaddrinfo(node.c_str(), service, &hints, &results);
    if(error) {
        YHCHAOS_LOG_DEBUG(g_logger) << "NetworkAddress::SearchFor getaddress(" << host << ", "
            << family << ", " << type << ") err=" << error << " errstr="
            << gai_strerror(error);
        return false;
    }

    next = results;
    while(next) {
        result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
        //YHCHAOS_LOG_INFO(g_logger) << ((sockaddr_in*)next->ai_addr)->sin_addr.s_addr;
        next = next->ai_next;
    }

    freeaddrinfo(results);
    return !result.empty();
}

bool NetworkAddress::GetInterfaceNetworkAddresses(std::multimap<std::string
                    ,std::pair<NetworkAddress::ptr, uint32_t> >& result,
                    int family) {
    struct ifaddrs *next, *results;
    if(getifaddrs(&results) != 0) {
        YHCHAOS_LOG_DEBUG(g_logger) << "NetworkAddress::GetInterfaceNetworkAddresses getifaddrs "
            " err=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    try {
        for(next = results; next; next = next->ifa_next) {
            NetworkAddress::ptr addr;
            uint32_t prefix_len = ~0u;
            if(family != AF_UNSPEC && family != next->ifa_addr->sa_family) {
                continue;
            }
            switch(next->ifa_addr->sa_family) {
                case AF_INET:
                    {
                        addr = Create(next->ifa_addr, sizeof(sockaddr_in));
                        uint32_t netmask = ((sockaddr_in*)next->ifa_netmask)->sin_addr.s_addr;
                        //有多少个1
                        prefix_len = CountBytes(netmask);
                    }
                    break;
                case AF_INET6:
                    {
                        addr = Create(next->ifa_addr, sizeof(sockaddr_in6));
                        sin6_addr& netmask = ((sockaddr_in6*)next->ifa_netmask)->sin6_addr;
                        prefix_len = 0;
                        for(int i = 0; i < 16; ++i) {
                            prefix_len += CountBytes(netmask.s6_addr[i]);
                        }
                    }
                    break;
                default:
                    break;
            }

            if(addr) {
                result.insert(std::make_pair(next->ifa_name,
                            std::make_pair(addr, prefix_len)));
            }
        }
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "NetworkAddress::GetInterfaceNetworkAddresses exception";
        freeifaddrs(results);
        return false;
    }
    freeifaddrs(results);
    return !result.empty();
}

bool NetworkAddress::GetInterfaceNetworkAddresses(std::vector<std::pair<NetworkAddress::ptr, uint32_t> >&result
                    ,const std::string& iface, int family) {
    if(iface.empty() || iface == "*") {
        if(family == AF_INET || family == AF_UNSPEC) {
            result.push_back(std::make_pair(NetworkAddress::ptr(new IPv4NetworkAddress()), 0u));
        }
        if(family == AF_INET6 || family == AF_UNSPEC) {
            result.push_back(std::make_pair(NetworkAddress::ptr(new IPv6NetworkAddress()), 0u));
        }
        return true;
    }

    std::multimap<std::string
          ,std::pair<NetworkAddress::ptr, uint32_t> > results;

    if(!GetInterfaceNetworkAddresses(results, family)) {
        return false;
    }

    auto its = results.equal_range(iface);
    for(; its.first != its.second; ++its.first) {
        result.push_back(its.first->second);
    }
    return !result.empty();
}

int NetworkAddress::getFamily() const {
    return getAddr()->sa_family;
}

std::string NetworkAddress::toString() const {
    std::stringstream ss;
    insert(ss);
    return ss.str();
}

NetworkAddress::ptr NetworkAddress::Create(const sockaddr* addr, socklen_t addrlen) {
    if(addr == nullptr) {
        return nullptr;
    }

    NetworkAddress::ptr result;
    switch(addr->sa_family) {
        case AF_INET:
            result.reset(new IPv4NetworkAddress(*(const sockaddr_in*)addr));
            break;
        case AF_INET6:
            result.reset(new IPv6NetworkAddress(*(const sockaddr_in6*)addr));
            break;
        default:
            result.reset(new UnknownNetworkAddress(*addr));
            break;
    }
    return result;
}

bool NetworkAddress::operator<(const NetworkAddress& rhs) const {
    socklen_t minlen = std::min(getAddrLen(), rhs.getAddrLen());
    int result = memcmp(getAddr(), rhs.getAddr(), minlen);
    if(result < 0) {
        return true;
    } else if(result > 0) {
        return false;
    } else if(getAddrLen() < rhs.getAddrLen()) {
        return true;
    }
    return false;
}

bool NetworkAddress::operator==(const NetworkAddress& rhs) const {
    return getAddrLen() == rhs.getAddrLen()
        && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
}

bool NetworkAddress::operator!=(const NetworkAddress& rhs) const {
    return !(*this == rhs);
}

IPNetworkAddress::ptr IPNetworkAddress::Create(const char* address, uint16_t port) {
    addrinfo hints, *results;
    memset(&hints, 0, sizeof(addrinfo));

    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = AF_UNSPEC;

    int error = getaddrinfo(address, NULL, &hints, &results);
    if(error) {
        YHCHAOS_LOG_DEBUG(g_logger) << "IPNetworkAddress::Create(" << address
            << ", " << port << ") error=" << error
            << " errno=" << errno << " errstr=" << strerror(errno);
        return nullptr;
    }

    try {
        IPNetworkAddress::ptr result = std::dynamic_pointer_cast<IPNetworkAddress>(
                NetworkAddress::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
        if(result) {
            result->setPort(port);
        }
        freeaddrinfo(results);
        return result;
    } catch (...) {
        freeaddrinfo(results);
        return nullptr;
    }
}

IPv4NetworkAddress::ptr IPv4NetworkAddress::Create(const char* address, uint16_t port) {
    IPv4NetworkAddress::ptr rt(new IPv4NetworkAddress);
    rt->m_addr.sin_port = swapbyteOnLittleEndian(port);
    int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
    if(result <= 0) {
        YHCHAOS_LOG_DEBUG(g_logger) << "IPv4NetworkAddress::Create(" << address << ", "
                << port << ") rt=" << result << " errno=" << errno
                << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv4NetworkAddress::IPv4NetworkAddress(const sockaddr_in& address) {
    m_addr = address;
}

IPv4NetworkAddress::IPv4NetworkAddress(uint32_t address, uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_port = swapbyteOnLittleEndian(port);
    m_addr.sin_addr.s_addr = swapbyteOnLittleEndian(address);
}

sockaddr* IPv4NetworkAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

const sockaddr* IPv4NetworkAddress::getAddr() const {
    return (sockaddr*)&m_addr;
}

socklen_t IPv4NetworkAddress::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& IPv4NetworkAddress::insert(std::ostream& os) const {
    uint32_t addr = swapbyteOnLittleEndian(m_addr.sin_addr.s_addr);
    os << ((addr >> 24) & 0xff) << "."
       << ((addr >> 16) & 0xff) << "."
       << ((addr >> 8) & 0xff) << "."
       << (addr & 0xff);
    os << ":" << swapbyteOnLittleEndian(m_addr.sin_port);
    return os;
}

IPNetworkAddress::ptr IPv4NetworkAddress::broadcastNetworkAddress(uint32_t prefix_len) {
    if(prefix_len > 32) {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr |= swapbyteOnLittleEndian(
            CreateMask<uint32_t>(prefix_len));
    return IPv4NetworkAddress::ptr(new IPv4NetworkAddress(baddr));
}

IPNetworkAddress::ptr IPv4NetworkAddress::networdNetworkAddress(uint32_t prefix_len) {
    if(prefix_len > 32) {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr &= swapbyteOnLittleEndian(
            CreateMask<uint32_t>(prefix_len));
    return IPv4NetworkAddress::ptr(new IPv4NetworkAddress(baddr));
}

IPNetworkAddress::ptr IPv4NetworkAddress::subnetMask(uint32_t prefix_len) {
    sockaddr_in subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin_family = AF_INET;
    subnet.sin_addr.s_addr = ~swapbyteOnLittleEndian(CreateMask<uint32_t>(prefix_len));
    return IPv4NetworkAddress::ptr(new IPv4NetworkAddress(subnet));
}

uint32_t IPv4NetworkAddress::getPort() const {
    return swapbyteOnLittleEndian(m_addr.sin_port);
}

void IPv4NetworkAddress::setPort(uint16_t v) {
    m_addr.sin_port = swapbyteOnLittleEndian(v);
}

IPv6NetworkAddress::ptr IPv6NetworkAddress::Create(const char* address, uint16_t port) {
    IPv6NetworkAddress::ptr rt(new IPv6NetworkAddress);
    rt->m_addr.sin6_port = swapbyteOnLittleEndian(port);
    int result = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
    if(result <= 0) {
        YHCHAOS_LOG_DEBUG(g_logger) << "IPv6NetworkAddress::Create(" << address << ", "
                << port << ") rt=" << result << " errno=" << errno
                << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv6NetworkAddress::IPv6NetworkAddress() {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
}

IPv6NetworkAddress::IPv6NetworkAddress(const sockaddr_in6& address) {
    m_addr = address;
}

IPv6NetworkAddress::IPv6NetworkAddress(const uint8_t address[16], uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
    m_addr.sin6_port = swapbyteOnLittleEndian(port);
    memcpy(&m_addr.sin6_addr.s6_addr, address, 16);
}

sockaddr* IPv6NetworkAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

const sockaddr* IPv6NetworkAddress::getAddr() const {
    return (sockaddr*)&m_addr;
}

socklen_t IPv6NetworkAddress::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& IPv6NetworkAddress::insert(std::ostream& os) const {
    os << "[";
    uint16_t* addr = (uint16_t*)m_addr.sin6_addr.s6_addr;
    bool used_zeros = false;
    for(size_t i = 0; i < 8; ++i) {
        if(addr[i] == 0 && !used_zeros) {
            continue;
        }
        if(i && addr[i - 1] == 0 && !used_zeros) {
            os << ":";
            used_zeros = true;
        }
        if(i) {
            os << ":";
        }
        os << std::hex << (int)swapbyteOnLittleEndian(addr[i]) << std::dec;
    }

    if(!used_zeros && addr[7] == 0) {
        os << "::";
    }

    os << "]:" << swapbyteOnLittleEndian(m_addr.sin6_port);
    return os;
}

IPNetworkAddress::ptr IPv6NetworkAddress::broadcastNetworkAddress(uint32_t prefix_len) {
    sockaddr_in6 baddr(m_addr);
    baddr.sin6_addr.s6_addr[prefix_len / 8] |=
        CreateMask<uint8_t>(prefix_len % 8);
    for(int i = prefix_len / 8 + 1; i < 16; ++i) {
        baddr.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6NetworkAddress::ptr(new IPv6NetworkAddress(baddr));
}

IPNetworkAddress::ptr IPv6NetworkAddress::networdNetworkAddress(uint32_t prefix_len) {
    sockaddr_in6 baddr(m_addr);
    baddr.sin6_addr.s6_addr[prefix_len / 8] &=
        CreateMask<uint8_t>(prefix_len % 8);
    for(int i = prefix_len / 8 + 1; i < 16; ++i) {
        baddr.sin6_addr.s6_addr[i] = 0x00;
    }
    return IPv6NetworkAddress::ptr(new IPv6NetworkAddress(baddr));
}

IPNetworkAddress::ptr IPv6NetworkAddress::subnetMask(uint32_t prefix_len) {
    sockaddr_in6 subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin6_family = AF_INET6;
    subnet.sin6_addr.s6_addr[prefix_len /8] =
        ~CreateMask<uint8_t>(prefix_len % 8);

    for(uint32_t i = 0; i < prefix_len / 8; ++i) {
        subnet.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6NetworkAddress::ptr(new IPv6NetworkAddress(subnet));
}

uint32_t IPv6NetworkAddress::getPort() const {
    return swapbyteOnLittleEndian(m_addr.sin6_port);
}

void IPv6NetworkAddress::setPort(uint16_t v) {
    m_addr.sin6_port = swapbyteOnLittleEndian(v);
}

static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un*)0)->sun_path) - 1;

UnixNetworkAddress::UnixNetworkAddress() {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    //计算sockaddr_un中sun_path的偏移量
    m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
}

UnixNetworkAddress::UnixNetworkAddress(const std::string& path) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    m_length = path.size() + 1;

    if(!path.empty() && path[0] == '\0') {
        --m_length;
    }

    if(m_length > sizeof(m_addr.sun_path)) {
        throw std::logic_error("path too long");
    }
    memcpy(m_addr.sun_path, path.c_str(), m_length);
    m_length += offsetof(sockaddr_un, sun_path);
}

void UnixNetworkAddress::setAddrLen(uint32_t v) {
    m_length = v;
}

sockaddr* UnixNetworkAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

const sockaddr* UnixNetworkAddress::getAddr() const {
    return (sockaddr*)&m_addr;
}

socklen_t UnixNetworkAddress::getAddrLen() const {
    return m_length;
}

std::string UnixNetworkAddress::getPath() const {
    std::stringstream ss;
    if(m_length > offsetof(sockaddr_un, sun_path)
            && m_addr.sun_path[0] == '\0') {
        ss << "\\0" << std::string(m_addr.sun_path + 1,
                m_length - offsetof(sockaddr_un, sun_path) - 1);
    } else {
        ss << m_addr.sun_path;
    }
    return ss.str();
}

std::ostream& UnixNetworkAddress::insert(std::ostream& os) const {
    if(m_length > offsetof(sockaddr_un, sun_path)
            && m_addr.sun_path[0] == '\0') {
        return os << "\\0" << std::string(m_addr.sun_path + 1,
                m_length - offsetof(sockaddr_un, sun_path) - 1);
    }
    return os << m_addr.sun_path;
}

UnknownNetworkAddress::UnknownNetworkAddress(int family) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sa_family = family;
}

UnknownNetworkAddress::UnknownNetworkAddress(const sockaddr& addr) {
    m_addr = addr;
}

sockaddr* UnknownNetworkAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

const sockaddr* UnknownNetworkAddress::getAddr() const {
    return &m_addr;
}

socklen_t UnknownNetworkAddress::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& UnknownNetworkAddress::insert(std::ostream& os) const {
    os << "[UnknownNetworkAddress family=" << m_addr.sa_family << "]";
    return os;
}

std::ostream& operator<<(std::ostream& os, const NetworkAddress& addr) {
    return addr.insert(os);
}

}
