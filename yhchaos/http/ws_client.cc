#include "ws_client.h"

namespace yhchaos {
namespace http {

WClient::WClient(Sock::ptr sock, bool owner) 
    :HttpClient(sock, owner) {
}

std::pair<HttpRes::ptr, WClient::ptr> WClient::Create(const std::string& url
                                       ,uint64_t timeout_ms
                                       ,const std::map<std::string, std::string>& headers) {
    UriDesc::ptr uri = UriDesc::Create(url);
    if(!uri) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::INVALID_URL,
                    nullptr, "invalid url:" + url), nullptr);
    }
    return Create(uri, timeout_ms, headers);
}

std::pair<HttpRes::ptr, WClient::ptr> WClient::Create(UriDesc::ptr uri
                                    ,uint64_t timeout_ms
                                    ,const std::map<std::string, std::string>& headers) {
    NetworkAddress::ptr addr = uri->createNetworkAddress();
    if(!addr) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::INVALID_HOST
                , nullptr, "invalid host: " + uri->getHost()), nullptr);
    }
    Sock::ptr sock = Sock::CreateTCP(addr);
    if(!sock) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::CREATE_SOCKET_ERROR
                , nullptr, "create socket fail: " + addr->toString()
                        + " errno=" + std::to_string(errno)
                        + " errstr=" + std::string(strerror(errno))), nullptr);
    }
    if(!sock->connect(addr)) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::CONNECT_FAIL
                , nullptr, "connect fail: " + addr->toString()), nullptr);
    }
    sock->setRecvTimeout(timeout_ms);
    WClient::ptr conn = std::make_shared<WClient>(sock);

    HttpReq::ptr req = std::make_shared<HttpReq>();
    req->setPath(uri->getPath());
    req->setQuery(uri->getQuery());
    req->setFragment(uri->getFragment());
    req->setMethod(HMethod::GET);
    bool has_host = false;
    bool has_conn = false;
    for(auto& i : headers) {
        if(strcasecmp(i.first.c_str(), "connection") == 0) {
            has_conn = true;
        } else if(!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }

        req->setHeader(i.first, i.second);
    }
    req->setWebsocket(true);
    if(!has_conn) {
        req->setHeader("connection", "Upgrade");
    }
    req->setHeader("Upgrade", "websocket");
    req->setHeader("Sec-webSock-Version", "13");
    req->setHeader("Sec-webSock-Key", yhchaos::base64encode(random_string(16)));
    if(!has_host) {
        req->setHeader("Host", uri->getHost());
    }

   int rt = conn->sendReq(req);
    if(rt == 0) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::SEND_CLOSE_BY_PEER
                , nullptr, "send request closed by peer: " + addr->toString()), nullptr);
    }
    if(rt < 0) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::SEND_SOCKET_ERROR
                    , nullptr, "send request socket error errno=" + std::to_string(errno)
                    + " errstr=" + std::string(strerror(errno))), nullptr);
    }
    auto rsp = conn->recvRsp();
    if(!rsp) {
        return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::TIMEOUT
                    , nullptr, "recv response timeout: " + addr->toString()
                    + " timeout_ms:" + std::to_string(timeout_ms)), nullptr);
    }

    if(rsp->getStatus() != HStatus::SWITCHING_PROTOCOLS) {
        return std::make_pair(std::make_shared<HttpRes>(50
                    , rsp, "not websocket server " + addr->toString()), nullptr);
    }
    return std::make_pair(std::make_shared<HttpRes>((int)HttpRes::Error::OK
                , rsp, "ok"), conn);
}

WFrameMSG::ptr WClient::recvMSG() {
    return WRecvMSG(this, true);
}

int32_t WClient::sendMSG(WFrameMSG::ptr msg, bool fin) {
    return WSendMSG(this, msg, true, fin);
}

int32_t WClient::sendMSG(const std::string& msg, int32_t opcode, bool fin) {
    return WSendMSG(this, std::make_shared<WFrameMSG>(opcode, msg), true, fin);
}

int32_t WClient::ping() {
    return WPing(this);
}

int32_t WClient::pong() {
    return WPong(this);
}

}
}
