#include "tcpserver.h"
#include "appconfig.h"
#include "log.h"

namespace yhchaos {

static yhchaos::AppConfigVar<uint64_t>::ptr g_tcp_server_read_timeout =
    yhchaos::AppConfig::SearchFor("tcp_server.read_timeout", (uint64_t)(60 * 1000 * 2),
            "tcp server read timeout");

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

TcpSvr::TcpSvr(yhchaos::IOCoScheduler* worker,
                    yhchaos::IOCoScheduler* io_worker,
                    yhchaos::IOCoScheduler* accept_worker)
    :m_worker(worker)
    ,m_ioWorker(io_worker)
    ,m_acceptWorker(accept_worker)
    ,m_recvTimeout(g_tcp_server_read_timeout->getValue())
    ,m_name("yhchaos/1.0.0")
    ,m_isStop(true) {
}

TcpSvr::~TcpSvr() {
    for(auto& i : m_socks) {
        i->close();
    }
    m_socks.clear();
}

void TcpSvr::setConf(const TcpSvrConf& v) {
    m_conf.reset(new TcpSvrConf(v));
}

bool TcpSvr::bind(yhchaos::NetworkAddress::ptr addr, bool ssl) {
    std::vector<NetworkAddress::ptr> addrs;
    std::vector<NetworkAddress::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails, ssl);
}

bool TcpSvr::bind(const std::vector<NetworkAddress::ptr>& addrs
                        ,std::vector<NetworkAddress::ptr>& fails
                        ,bool ssl) {
    m_ssl = ssl;
    for(auto& addr : addrs) {
        Sock::ptr sock = ssl ? SSLSock::CreateTCP(addr) : Sock::CreateTCP(addr);
        if(!sock->bind(addr)) {
            YHCHAOS_LOG_ERROR(g_logger) << "bind fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        if(!sock->listen()) {
            YHCHAOS_LOG_ERROR(g_logger) << "listen fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        m_socks.push_back(sock);
    }

    if(!fails.empty()) {
        m_socks.clear();
        return false;
    }

    for(auto& i : m_socks) {
        YHCHAOS_LOG_INFO(g_logger) << "type=" << m_type
            << " name=" << m_name
            << " ssl=" << m_ssl
            << " server bind success: " << *i;
    }
    return true;
}

void TcpSvr::startAccept(Sock::ptr sock) {
    while(!m_isStop) {
        Sock::ptr client = sock->accept();
        if(client) {
            client->setRecvTimeout(m_recvTimeout);
            m_ioWorker->coschedule(std::bind(&TcpSvr::handleClient,
                        shared_from_this(), client));
        } else {
            YHCHAOS_LOG_ERROR(g_logger) << "accept errno=" << errno
                << " errstr=" << strerror(errno);
        }
    }
}

bool TcpSvr::start() {
    if(!m_isStop) {
        return true;
    }
    m_isStop = false;
    for(auto& sock : m_socks) {
        m_acceptWorker->coschedule(std::bind(&TcpSvr::startAccept,
                    shared_from_this(), sock));
    }
    return true;
}

void TcpSvr::stop() {
    m_isStop = true;
    auto self = shared_from_this();
    m_acceptWorker->coschedule([this, self]() {
        for(auto& sock : m_socks) {
            sock->cancelAll();
            sock->close();
        }
        m_socks.clear();
    });
}

void TcpSvr::handleClient(Sock::ptr client) {
    YHCHAOS_LOG_INFO(g_logger) << "handleClient: " << *client;
}

bool TcpSvr::loadCertificates(const std::string& cert_file, const std::string& key_file) {
    for(auto& i : m_socks) {
        auto ssl_socket = std::dynamic_pointer_cast<SSLSock>(i);
        if(ssl_socket) {
            if(!ssl_socket->loadCertificates(cert_file, key_file)) {
                return false;
            }
        }
    }
    return true;
}

std::string TcpSvr::toString(const std::string& prefix) {
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << m_name << " ssl=" << m_ssl
       << " worker=" << (m_worker ? m_worker->getName() : "")
       << " accept=" << (m_acceptWorker ? m_acceptWorker->getName() : "")
       << " recv_timeout=" << m_recvTimeout << "]" << std::endl;
    std::string pfx = prefix.empty() ? "    " : prefix;
    for(auto& i : m_socks) {
        ss << pfx << pfx << *i << std::endl;
    }
    return ss.str();
}

}
