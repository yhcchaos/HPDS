#include "sock_stream.h"
#include "yhchaos/util.h"

namespace yhchaos {

SockStream::SockStream(Sock::ptr sock, bool owner)
    :m_socket(sock)
    ,m_owner(owner) {
}

SockStream::~SockStream() {
    if(m_owner && m_socket) {
        m_socket->close();
    }
}

bool SockStream::isConnected() const {
    return m_socket && m_socket->isConnected();
}

int SockStream::read(void* buffer, size_t length) {
    if(!isConnected()) {
        return -1;
    }
    return m_socket->recv(buffer, length);
}

int SockStream::read(ByteBuffer::ptr ba, size_t length) {
    if(!isConnected()) {
        return -1;
    }
    std::vector<iovec> iovs;
    //ba为读取的长度
    ba->getWriteBuffers(iovs, length);
    int rt = m_socket->recv(&iovs[0], iovs.size());
    if(rt > 0) {
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

int SockStream::write(const void* buffer, size_t length) {
    if(!isConnected()) {
        return -1;
    }
    return m_socket->send(buffer, length);
}

int SockStream::write(ByteBuffer::ptr ba, size_t length) {
    if(!isConnected()) {
        return -1;
    }
    std::vector<iovec> iovs;
    ba->getReadBuffers(iovs, length);
    int rt = m_socket->send(&iovs[0], iovs.size());
    if(rt > 0) {
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

void SockStream::close() {
    if(m_socket) {
        m_socket->close();
    }
}

NetworkAddress::ptr SockStream::getRemoteNetworkAddress() {
    if(m_socket) {
        return m_socket->getRemoteNetworkAddress();
    }
    return nullptr;
}

NetworkAddress::ptr SockStream::getLocalNetworkAddress() {
    if(m_socket) {
        return m_socket->getLocalNetworkAddress();
    }
    return nullptr;
}

std::string SockStream::getRemoteNetworkAddressString() {
    auto addr = getRemoteNetworkAddress();
    if(addr) {
        return addr->toString();
    }
    return "";
}

std::string SockStream::getLocalNetworkAddressString() {
    auto addr = getLocalNetworkAddress();
    if(addr) {
        return addr->toString();
    }
    return "";
}

}
