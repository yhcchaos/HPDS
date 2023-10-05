#include "sock.h"
#include "iocoscheduler.h"
#include "file_manager.h"
#include "log.h"
#include "macro.h"
#include "hookfunc.h"
#include <limits.h>

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

Sock::ptr Sock::CreateTCP(yhchaos::NetworkAddress::ptr address) {
    Sock::ptr sock(new Sock(address->getFamily(), TCP, 0));
    return sock;
}

Sock::ptr Sock::CreateUDP(yhchaos::NetworkAddress::ptr address) {
    Sock::ptr sock(new Sock(address->getFamily(), UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Sock::ptr Sock::CreateTCPSock() {
    Sock::ptr sock(new Sock(IPv4, TCP, 0));
    return sock;
}

Sock::ptr Sock::CreateUDPSock() {
    Sock::ptr sock(new Sock(IPv4, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Sock::ptr Sock::CreateTCPSock6() {
    Sock::ptr sock(new Sock(IPv6, TCP, 0));
    return sock;
}

Sock::ptr Sock::CreateUDPSock6() {
    Sock::ptr sock(new Sock(IPv6, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Sock::ptr Sock::CreateUnixTCPSock() {
    Sock::ptr sock(new Sock(UNIX, TCP, 0));
    return sock;
}

Sock::ptr Sock::CreateUnixUDPSock() {
    Sock::ptr sock(new Sock(UNIX, UDP, 0));
    return sock;
}

Sock::Sock(int family, int type, int protocol)
    :m_sock(-1)
    ,m_family(family)
    ,m_type(type)
    ,m_protocol(protocol)
    ,m_isConnected(false) {
}

Sock::~Sock() {
    close();
}

int64_t Sock::getSendTimeout() {
    FileContext::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if(ctx) {
        return ctx->getTimeout(SO_SNDTIMEO);
    }
    return -1;
}

void Sock::setSendTimeout(int64_t v) {
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Sock::getRecvTimeout() {
    FileContext::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if(ctx) {
        return ctx->getTimeout(SO_RCVTIMEO);
    }
    return -1;
}

void Sock::setRecvTimeout(int64_t v) {
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Sock::getOption(int level, int option, void* result, socklen_t* len) {
    int rt = getsockopt(m_sock, level, option, result, (socklen_t*)len);
    if(rt) {
        YHCHAOS_LOG_DEBUG(g_logger) << "getOption sock=" << m_sock
            << " level=" << level << " option=" << option
            << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Sock::setOption(int level, int option, const void* result, socklen_t len) {
    if(setsockopt(m_sock, level, option, result, (socklen_t)len)) {
        YHCHAOS_LOG_DEBUG(g_logger) << "setOption sock=" << m_sock
            << " level=" << level << " option=" << option
            << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

Sock::ptr Sock::accept() {
    Sock::ptr sock(new Sock(m_family, m_type, m_protocol));
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if(newsock == -1) {
        YHCHAOS_LOG_ERROR(g_logger) << "accept(" << m_sock << ") errno="
            << errno << " errstr=" << strerror(errno);
        return nullptr;
    }
    if(sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

bool Sock::init(int sock) {
    FileContext::ptr ctx = FdMgr::GetInstance()->get(sock);
    if(ctx && ctx->isSock() && !ctx->isClose()) {
        m_sock = sock;
        m_isConnected = true;
        initSock();
        getLocalNetworkAddress();
        getRemoteNetworkAddress();
        return true;
    }
    return false;
}

bool Sock::bind(const NetworkAddress::ptr addr) {
    //m_localNetworkAddress = addr;
    if(!isValid()) {
        newSock();
        if(YHCHAOS_UNLIKELY(!isValid())) {
            return false;
        }
    }

    if(YHCHAOS_UNLIKELY(addr->getFamily() != m_family)) {
        YHCHAOS_LOG_ERROR(g_logger) << "bind sock.family("
            << m_family << ") addr.family(" << addr->getFamily()
            << ") not equal, addr=" << addr->toString();
        return false;
    }
    //这个addr是不是unix套接字
    UnixNetworkAddress::ptr uaddr = std::dynamic_pointer_cast<UnixNetworkAddress>(addr);
    if(uaddr) {
        Sock::ptr sock = Sock::CreateUnixTCPSock();
        //如果能连接的上，证明uaddr已经绑定了
        if(sock->connect(uaddr)) {
            return false;
        } else {
            //如果路径名存在，那么bind就会失败，所以先调用unlink删除路径名
            yhchaos::FSUtil::Unlink(uaddr->getPath(), true);
        }
    }

    if(::bind(m_sock, addr->getAddr(), addr->getAddrLen())) {
        YHCHAOS_LOG_ERROR(g_logger) << "bind error errrno=" << errno
            << " errstr=" << strerror(errno);
        return false;
    }
    getLocalNetworkAddress();
    return true;
}

bool Sock::reconnect(uint64_t timeout_ms) {
    if(!m_remoteNetworkAddress) {
        YHCHAOS_LOG_ERROR(g_logger) << "reconnect m_remoteNetworkAddress is null";
        return false;
    }
    m_localNetworkAddress.reset();
    return connect(m_remoteNetworkAddress, timeout_ms);
}

bool Sock::connect(const NetworkAddress::ptr addr, uint64_t timeout_ms) {
    m_remoteNetworkAddress = addr;
    if(!isValid()) {
        newSock();
        if(YHCHAOS_UNLIKELY(!isValid())) {
            return false;
        }
    }

    if(YHCHAOS_UNLIKELY(addr->getFamily() != m_family)) {
        YHCHAOS_LOG_ERROR(g_logger) << "connect sock.family("
            << m_family << ") addr.family(" << addr->getFamily()
            << ") not equal, addr=" << addr->toString();
        return false;
    }

    if(timeout_ms == (uint64_t)-1) {
        if(::connect(m_sock, addr->getAddr(), addr->getAddrLen())) {
            YHCHAOS_LOG_ERROR(g_logger) << "sock=" << m_sock << " connect(" << addr->toString()
                << ") error errno=" << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    } else {
        if(::connect_with_timeout(m_sock, addr->getAddr(), addr->getAddrLen(), timeout_ms)) {
            YHCHAOS_LOG_ERROR(g_logger) << "sock=" << m_sock << " connect(" << addr->toString()
                << ") timeout=" << timeout_ms << " error errno="
                << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    }
    m_isConnected = true;
    getRemoteNetworkAddress();
    getLocalNetworkAddress();
    return true;
}

bool Sock::listen(int backlog) {
    if(!isValid()) {
        YHCHAOS_LOG_ERROR(g_logger) << "listen error sock=-1";
        return false;
    }
    if(::listen(m_sock, backlog)) {
        YHCHAOS_LOG_ERROR(g_logger) << "listen error errno=" << errno
            << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Sock::close() {
    if(!m_isConnected && m_sock == -1) {
        return true;
    }
    m_isConnected = false;
    if(m_sock != -1) {
        ::close(m_sock);
        m_sock = -1;
    }
    return false;
}

int Sock::send(const void* buffer, size_t length, int flags) {
    if(isConnected()) {
        return ::send(m_sock, buffer, length, flags);
    }
    return -1;
}

int Sock::send(const iovec* buffers, size_t length, int flags) {
    if(isConnected()) {

        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Sock::sendTo(const void* buffer, size_t length, const NetworkAddress::ptr to, int flags) {
    if(isConnected()) {
        return ::sendto(m_sock, buffer, length, flags, to->getAddr(), to->getAddrLen());
    }
    return -1;
}

int Sock::sendTo(const iovec* buffers, size_t length, const NetworkAddress::ptr to, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = to->getAddr();
        msg.msg_namelen = to->getAddrLen();
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Sock::recv(void* buffer, size_t length, int flags) {
    if(isConnected()) {
        return ::recv(m_sock, buffer, length, flags);
    }
    return -1;
}

int Sock::recv(iovec* buffers, size_t length, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Sock::recvFrom(void* buffer, size_t length, NetworkAddress::ptr from, int flags) {
    if(isConnected()) {
        socklen_t len = from->getAddrLen();
        return ::recvfrom(m_sock, buffer, length, flags, from->getAddr(), &len);
    }
    return -1;
}

int Sock::recvFrom(iovec* buffers, size_t length, NetworkAddress::ptr from, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = from->getAddr();
        msg.msg_namelen = from->getAddrLen();
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

NetworkAddress::ptr Sock::getRemoteNetworkAddress() {
    if(m_remoteNetworkAddress) {
        return m_remoteNetworkAddress;
    }

    NetworkAddress::ptr result;
    switch(m_family) {
        case AF_INET:
            result.reset(new IPv4NetworkAddress());
            break;
        case AF_INET6:
            result.reset(new IPv6NetworkAddress());
            break;
        case AF_UNIX:
            result.reset(new UnixNetworkAddress());
            break;
        default:
            result.reset(new UnknownNetworkAddress(m_family));
            break;
    }
    socklen_t addrlen = result->getAddrLen();
    if(getpeername(m_sock, result->getAddr(), &addrlen)) {
        //YHCHAOS_LOG_ERROR(g_logger) << "getpeername error sock=" << m_sock
        //    << " errno=" << errno << " errstr=" << strerror(errno);
        return NetworkAddress::ptr(new UnknownNetworkAddress(m_family));
    }
    if(m_family == AF_UNIX) {
        UnixNetworkAddress::ptr addr = std::dynamic_pointer_cast<UnixNetworkAddress>(result);
        addr->setAddrLen(addrlen);
    }
    m_remoteNetworkAddress = result;
    return m_remoteNetworkAddress;
}

NetworkAddress::ptr Sock::getLocalNetworkAddress() {
    if(m_localNetworkAddress) {
        return m_localNetworkAddress;
    }

    NetworkAddress::ptr result;
    switch(m_family) {
        case AF_INET:
            result.reset(new IPv4NetworkAddress());
            break;
        case AF_INET6:
            result.reset(new IPv6NetworkAddress());
            break;
        case AF_UNIX:
            result.reset(new UnixNetworkAddress());
            break;
        default:
            result.reset(new UnknownNetworkAddress(m_family));
            break;
    }
    socklen_t addrlen = result->getAddrLen();
    //获得套接字的本地协议地址
    if(getsockname(m_sock, result->getAddr(), &addrlen)) {
        YHCHAOS_LOG_ERROR(g_logger) << "getsockname error sock=" << m_sock
            << " errno=" << errno << " errstr=" << strerror(errno);
        return NetworkAddress::ptr(new UnknownNetworkAddress(m_family));
    }
    if(m_family == AF_UNIX) {
        UnixNetworkAddress::ptr addr = std::dynamic_pointer_cast<UnixNetworkAddress>(result);
        addr->setAddrLen(addrlen);
    }
    m_localNetworkAddress = result;
    return m_localNetworkAddress;
}

bool Sock::isValid() const {
    return m_sock != -1;
}

int Sock::getError() {
    int error = 0;
    socklen_t len = sizeof(error);
    if(!getOption(SOL_SOCKET, SO_ERROR, &error, &len)) {
        error = errno;
    }
    return error;
}

std::ostream& Sock::dump(std::ostream& os) const {
    os << "[Sock sock=" << m_sock
       << " is_connected=" << m_isConnected
       << " family=" << m_family
       << " type=" << m_type
       << " protocol=" << m_protocol;
    if(m_localNetworkAddress) {
        os << " local_address=" << m_localNetworkAddress->toString();
    }
    if(m_remoteNetworkAddress) {
        os << " remote_address=" << m_remoteNetworkAddress->toString();
    }
    os << "]";
    return os;
}

std::string Sock::toString() const {
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

bool Sock::cancelRead() {
    return IOCoScheduler::GetThis()->cancelFdEvent(m_sock, yhchaos::IOCoScheduler::READ);
}

bool Sock::cancelWrite() {
    return IOCoScheduler::GetThis()->cancelFdEvent(m_sock, yhchaos::IOCoScheduler::WRITE);
}

bool Sock::cancelAccept() {
    return IOCoScheduler::GetThis()->cancelFdEvent(m_sock, yhchaos::IOCoScheduler::READ);
}

bool Sock::cancelAll() {
    return IOCoScheduler::GetThis()->cancelAll(m_sock);
}

void Sock::initSock() {
    int val = 1;
    setOption(SOL_SOCKET, SO_REUSEADDR, val);
    if(m_type == SOCK_STREAM) {
        setOption(IPPROTO_TCP, TCP_NODELAY, val);
    }
}

void Sock::newSock() {
    m_sock = socket(m_family, m_type, m_protocol);
    if(YHCHAOS_LIKELY(m_sock != -1)) {
        initSock();
    } else {
        YHCHAOS_LOG_ERROR(g_logger) << "socket(" << m_family
            << ", " << m_type << ", " << m_protocol << ") errno="
            << errno << " errstr=" << strerror(errno);
    }
}

namespace {

struct _SSLInit {
    _SSLInit() {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }
};

static _SSLInit s_init;

}

SSLSock::SSLSock(int family, int type, int protocol)
    :Sock(family, type, protocol) {
}

Sock::ptr SSLSock::accept() {
    SSLSock::ptr sock(new SSLSock(m_family, m_type, m_protocol));
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if(newsock == -1) {
        YHCHAOS_LOG_ERROR(g_logger) << "accept(" << m_sock << ") errno="
            << errno << " errstr=" << strerror(errno);
        return nullptr;
    }
    sock->m_ctx = m_ctx;
    if(sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

bool SSLSock::bind(const NetworkAddress::ptr addr) {
    return Sock::bind(addr);
}

bool SSLSock::connect(const NetworkAddress::ptr addr, uint64_t timeout_ms) {
    bool v = Sock::connect(addr, timeout_ms);
    if(v) {
        m_ctx.reset(SSL_CTX_new(SSLv23_client_method()), SSL_CTX_free);
        m_ssl.reset(SSL_new(m_ctx.get()),  SSL_free);
        SSL_set_fund(m_ssl.get(), m_sock);
        v = (SSL_connect(m_ssl.get()) == 1);
    }
    return v;
}

bool SSLSock::listen(int backlog) {
    return Sock::listen(backlog);
}

bool SSLSock::close() {
    return Sock::close();
}

int SSLSock::send(const void* buffer, size_t length, int flags) {
    if(m_ssl) {
        return SSL_write(m_ssl.get(), buffer, length);
    }
    return -1;
}

int SSLSock::send(const iovec* buffers, size_t length, int flags) {
    if(!m_ssl) {
        return -1;
    }
    int total = 0;
    for(size_t i = 0; i < length; ++i) {
        int tmp = SSL_write(m_ssl.get(), buffers[i].iov_base, buffers[i].iov_len);
        if(tmp <= 0) {
            return tmp;
        }
        total += tmp;
        if(tmp != (int)buffers[i].iov_len) {
            break;
        }
    }
    return total;
}

int SSLSock::sendTo(const void* buffer, size_t length, const NetworkAddress::ptr to, int flags) {
    YHCHAOS_ASSERT(false);
    return -1;
}

int SSLSock::sendTo(const iovec* buffers, size_t length, const NetworkAddress::ptr to, int flags) {
    YHCHAOS_ASSERT(false);
    return -1;
}

int SSLSock::recv(void* buffer, size_t length, int flags) {
    if(m_ssl) {
        return SSL_read(m_ssl.get(), buffer, length);
    }
    return -1;
}
//一次遍历iovec结构体数组，当有一个缓冲区没接受满时，就返回接受的总数居量
int SSLSock::recv(iovec* buffers, size_t length, int flags) {
    if(!m_ssl) {
        return -1;
    }
    int total = 0;
    for(size_t i = 0; i < length; ++i) {
        int tmp = SSL_read(m_ssl.get(), buffers[i].iov_base, buffers[i].iov_len);
        if(tmp <= 0) {
            return tmp;
        }
        total += tmp;
        if(tmp != (int)buffers[i].iov_len) {
            break;
        }
    }
    return total;
}

int SSLSock::recvFrom(void* buffer, size_t length, NetworkAddress::ptr from, int flags) {
    YHCHAOS_ASSERT(false);
    return -1;
}

int SSLSock::recvFrom(iovec* buffers, size_t length, NetworkAddress::ptr from, int flags) {
    YHCHAOS_ASSERT(false);
    return -1;
}

bool SSLSock::init(int sock) {
    bool v = Sock::init(sock);
    if(v) {
        //SSL_new 函数用于创建一个新的 SSL/TLS 连接对象，而 SSL_free 函数用于在 std::shared_ptr 引用计数归零时自动释放连接对象
        m_ssl.reset(SSL_new(m_ctx.get()),  SSL_free);
        //将 SSL 连接对象绑定到底层的套接字描述符，以便在加密和解密数据时使用。
        SSL_set_fund(m_ssl.get(), m_sock);
        //调用 SSL_accept 函数来尝试执行 SSL/TLS 握手，建立安全连接。如果握手成功，返回值为 1，赋值给变量 v。
        v = (SSL_accept(m_ssl.get()) == 1);
    }
    return v;
}

bool SSLSock::loadCertificates(const std::string& cert_file, const std::string& key_file) {
    m_ctx.reset(SSL_CTX_new(SSLv23_server_method()), SSL_CTX_free);
    //在 SSL/TLS 上下文中加载包含证书链的文件，以供 SSL/TLS 连接使用
    if(SSL_CTX_use_certificate_chain_file(m_ctx.get(), cert_file.c_str()) != 1) {
        YHCHAOS_LOG_ERROR(g_logger) << "SSL_CTX_use_certificate_chain_file("
            << cert_file << ") error";
        return false;
    }
    if(SSL_CTX_use_PrivateKey_file(m_ctx.get(), key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        YHCHAOS_LOG_ERROR(g_logger) << "SSL_CTX_use_PrivateKey_file("
            << key_file << ") error";
        return false;
    }
    if(SSL_CTX_check_private_key(m_ctx.get()) != 1) {
        YHCHAOS_LOG_ERROR(g_logger) << "SSL_CTX_check_private_key cert_file="
            << cert_file << " key_file=" << key_file;
        return false;
    }
    return true;
}

SSLSock::ptr SSLSock::CreateTCP(yhchaos::NetworkAddress::ptr address) {
    SSLSock::ptr sock(new SSLSock(address->getFamily(), TCP, 0));
    return sock;
}

SSLSock::ptr SSLSock::CreateTCPSock() {
    SSLSock::ptr sock(new SSLSock(IPv4, TCP, 0));
    return sock;
}

SSLSock::ptr SSLSock::CreateTCPSock6() {
    SSLSock::ptr sock(new SSLSock(IPv6, TCP, 0));
    return sock;
}

std::ostream& SSLSock::dump(std::ostream& os) const {
    os << "[SSLSock sock=" << m_sock
       << " is_connected=" << m_isConnected
       << " family=" << m_family
       << " type=" << m_type
       << " protocol=" << m_protocol;
    if(m_localNetworkAddress) {
        os << " local_address=" << m_localNetworkAddress->toString();
    }
    if(m_remoteNetworkAddress) {
        os << " remote_address=" << m_remoteNetworkAddress->toString();
    }
    os << "]";
    return os;
}

std::ostream& operator<<(std::ostream& os, const Sock& sock) {
    return sock.dump(os);
}

}
