#include "http_client.h"
#include "http_parser.h"
#include "yhchaos/log.h"
#include "yhchaos/streams/zlib_stream.h"

namespace yhchaos {
namespace http {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

std::string HttpRes::toString() const {
    std::stringstream ss;
    ss << "[HttpRes result=" << result
       << " error=" << error
       << " response=" << (response ? response->toString() : "nullptr")
       << "]";
    return ss.str();
}

HttpClient::HttpClient(Sock::ptr sock, bool owner)
    :SockStream(sock, owner) {
}

HttpClient::~HttpClient() {
    YHCHAOS_LOG_DEBUG(g_logger) << "HttpClient::~HttpClient";
}

HttpRsp::ptr HttpClient::recvRsp() {
    HttpRspParser::ptr parser(new HttpRspParser);
    uint64_t buff_size = HttpReqParser::GetHttpReqBufferSize();
    //uint64_t buff_size = 100;
    std::shared_ptr<char> buffer(
            new char[buff_size + 1], [](char* ptr){
                delete[] ptr;
            });
    char* data = buffer.get();
    int offset = 0;
    do {
        int len = read(data + offset, buff_size - offset);
        if(len <= 0) {
            close();
            return nullptr;
        }
        len += offset;
        data[len] = '\0';
        size_t nparse = parser->execute(data, len, false);
        if(parser->hasError()) {
            close();
            return nullptr;
        }
        offset = len - nparse;
        //表示buff占满了但是一点也没有解析
        if(offset == (int)buff_size) {
            close();
            return nullptr;
        }
        if(parser->isFinished()) {
            break;
        }
    } while(true);
    auto& client_parser = parser->getParser();
    std::string body;
    //一个块一个块的解析
    if(client_parser.chunked) {
        int len = offset;
        //依次读入块
        do {
            bool begin = true;
            //解析块长度字段，len指的是读取了但没有解析的数据长度
            do {
                if(!begin || len == 0) {
                    int rt = read(data + len, buff_size - len);
                    if(rt <= 0) {
                        close();
                        return nullptr;
                    }
                    len += rt;
                }
                data[len] = '\0';
                size_t nparse = parser->execute(data, len, true);
                if(parser->hasError()) {
                    close();
                    return nullptr;
                }
                len -= nparse;
                if(len == (int)buff_size) {
                    close();
                    return nullptr;
                }
                begin = false;
                //解析完报文中每个块的长度字段后表示解析完成，此时就会将
                //client_parser.content_len设置为当前块的长度
            } while(!parser->isFinished());
            //len -= 2;
            //解析第i个块的数据
            //这里的len是解析完块的长度部分之后，读入的数据长度，也就是块长度字段结束到已经读取的数据长度
            YHCHAOS_LOG_DEBUG(g_logger) << "content_len=" << client_parser.content_len;
            //如果读入但未处理的数据长度len大于等于当前块的长度，则将当前块的数据
            //client_parser.content_len读入body中，将下一个块读入但未处理的数据放到
            //while的下一个循环中处理
            if(client_parser.content_len + 2 <= len) {
                body.append(data, client_parser.content_len);
                memmove(data, data + client_parser.content_len + 2
                        , len - client_parser.content_len - 2);
                len -= client_parser.content_len + 2;
            //读入但未处理的数据长度len小于当前块的长度，则将其添加到body中，
            //然后将该块未读入的部分读入
            } else {
                body.append(data, len);
                int left = client_parser.content_len - len + 2;
                while(left > 0) {
                    int rt = read(data, left > (int)buff_size ? (int)buff_size : left);
                    if(rt <= 0) {
                        close();
                        return nullptr;
                    }
                    body.append(data, rt);
                    left -= rt;
                }
                body.resize(body.size() - 2);
                len = 0;
            }
        } while(!client_parser.chunks_done);
    } else {
        int64_t length = parser->getContentLength();
        if(length > 0) {
            body.resize(length);

            int len = 0;
            if(length >= offset) {
                memcpy(&body[0], data, offset);
                len = offset;
            } else {
                memcpy(&body[0], data, length);
                len = length;
            }
            length -= offset;
            if(length > 0) {
                if(readFixSize(&body[len], length) <= 0) {
                    close();
                    return nullptr;
                }
            }
        }
    }
    if(!body.empty()) {
        auto content_encoding = parser->getData()->getHeader("content-encoding");
        YHCHAOS_LOG_DEBUG(g_logger) << "content_encoding: " << content_encoding
            << " size=" << body.size();
        if(strcasecmp(content_encoding.c_str(), "gzip") == 0) {
            //gzip对回复进行压缩
            auto zs = ZlibStream::CreateGzip(false);
            zs->write(body.c_str(), body.size());
            zs->flush();
            zs->getRes().swap(body);
        } else if(strcasecmp(content_encoding.c_str(), "deflate") == 0) {
            //deflate对回复进行压缩
            auto zs = ZlibStream::CreateDeflate(false);
            zs->write(body.c_str(), body.size());
            zs->flush();
            zs->getRes().swap(body);
        }
        parser->getData()->setBody(body);
    }
    return parser->getData();
}

int HttpClient::sendReq(HttpReq::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    //std::cout << ss.str() << std::endl;
    return writeFixSize(data.c_str(), data.size());
}

HttpRes::ptr HttpClient::DoGet(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    UriDesc::ptr uri = UriDesc::Create(url);
    if(!uri) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::INVALID_URL
                , nullptr, "invalid url: " + url);
    }
    return DoGet(uri, timeout_ms, headers, body);
}

HttpRes::ptr HttpClient::DoGet(UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    return DoReq(HMethod::GET, uri, timeout_ms, headers, body);
}

HttpRes::ptr HttpClient::DoPost(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    UriDesc::ptr uri = UriDesc::Create(url);
    if(!uri) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::INVALID_URL
                , nullptr, "invalid url: " + url);
    }
    return DoPost(uri, timeout_ms, headers, body);
}

HttpRes::ptr HttpClient::DoPost(UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    return DoReq(HMethod::POST, uri, timeout_ms, headers, body);
}

HttpRes::ptr HttpClient::DoReq(HMethod method
                            , const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    UriDesc::ptr uri = UriDesc::Create(url);
    if(!uri) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::INVALID_URL
                , nullptr, "invalid url: " + url);
    }
    return DoReq(method, uri, timeout_ms, headers, body);
}

HttpRes::ptr HttpClient::DoReq(HMethod method
                            , UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body) {
    //根据uri创建httpReq对象
    HttpReq::ptr req = std::make_shared<HttpReq>();
    req->setPath(uri->getPath());
    req->setQuery(uri->getQuery());
    req->setFragment(uri->getFragment());
    req->setMethod(method);
    //headers中有host：xxx字段，则设置为true
    //如果没有，则从uri中获取host字段设置到req的headers中
    bool has_host = false;
    //如果uri中的headers中有connection=keep-alive，则设置req的close字段为false
    for(auto& i : headers) {
        if(strcasecmp(i.first.c_str(), "connection") == 0) {
            if(strcasecmp(i.second.c_str(), "keep-alive") == 0) {
                req->setClose(false);
            }
            continue;
        }

        if(!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }

        req->setHeader(i.first, i.second);
    }
    if(!has_host) {
        req->setHeader("Host", uri->getHost());
    }
    req->setBody(body);
    return DoReq(req, uri, timeout_ms);
}

HttpRes::ptr HttpClient::DoReq(HttpReq::ptr req
                            , UriDesc::ptr uri
                            , uint64_t timeout_ms) {
    bool is_ssl = uri->getScheme() == "https";
    NetworkAddress::ptr addr = uri->createNetworkAddress();
    if(!addr) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::INVALID_HOST
                , nullptr, "invalid host: " + uri->getHost());
    }
    Sock::ptr sock = is_ssl ? SSLSock::CreateTCP(addr) : Sock::CreateTCP(addr);
    if(!sock) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::CREATE_SOCKET_ERROR
                , nullptr, "create socket fail: " + addr->toString()
                        + " errno=" + std::to_string(errno)
                        + " errstr=" + std::string(strerror(errno)));
    }
    if(!sock->connect(addr)) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::CONNECT_FAIL
                , nullptr, "connect fail: " + addr->toString());
    }
    sock->setRecvTimeout(timeout_ms);
    HttpClient::ptr conn = std::make_shared<HttpClient>(sock);//owner=true
    int rt = conn->sendReq(req);
    if(rt == 0) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::SEND_CLOSE_BY_PEER
                , nullptr, "send request closed by peer: " + addr->toString());
    }
    if(rt < 0) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::SEND_SOCKET_ERROR
                    , nullptr, "send request socket error errno=" + std::to_string(errno)
                    + " errstr=" + std::string(strerror(errno)));
    }
    auto rsp = conn->recvRsp();
    if(!rsp) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::TIMEOUT
                    , nullptr, "recv response timeout: " + addr->toString()
                    + " timeout_ms:" + std::to_string(timeout_ms));
    }
    return std::make_shared<HttpRes>((int)HttpRes::Error::OK, rsp, "ok");
}

HttpClientPool::ptr HttpClientPool::Create(const std::string& uri
                                                   ,const std::string& vhost
                                                   ,uint32_t max_size
                                                   ,uint32_t max_alive_time
                                                   ,uint32_t max_request) {
    UriDesc::ptr turi = UriDesc::Create(uri);
    if(!turi) {
        YHCHAOS_LOG_ERROR(g_logger) << "invalid uri=" << uri;
    }
    return std::make_shared<HttpClientPool>(turi->getHost()
            , vhost, turi->getPort(), turi->getScheme() == "https"
            , max_size, max_alive_time, max_request);
}

HttpClientPool::HttpClientPool(const std::string& host
                                        ,const std::string& vhost
                                        ,uint32_t port
                                        ,bool is_https
                                        ,uint32_t max_size
                                        ,uint32_t max_alive_time
                                        ,uint32_t max_request)
    :m_host(host)
    ,m_vhost(vhost)
    ,m_port(port ? port : (is_https ? 443 : 80))
    ,m_maxSize(max_size)
    ,m_maxAliveTime(max_alive_time)
    ,m_maxReq(max_request)
    ,m_isHttps(is_https) {
}

HttpClient::ptr HttpClientPool::getClient() {
    uint64_t now_ms = yhchaos::GetCurrentMS();
    std::vector<HttpClient*> invalid_conns;
    HttpClient* ptr = nullptr;
    MtxType::Lock lock(m_mutex);
    //找到第一个已经连接的HttpClient
    while(!m_conns.empty()) {
        auto conn = *m_conns.begin();
        m_conns.pop_front();
        if(!conn->isConnected()) {
            invalid_conns.push_back(conn);
            continue;
        }
        if((conn->m_createTime + m_maxAliveTime) > now_ms) {
            invalid_conns.push_back(conn);
            continue;
        }
        ptr = conn;
        break;
    }
    lock.unlock();
    for(auto i : invalid_conns) {
        delete i;
    }
    m_total -= invalid_conns.size();

    if(!ptr) {
        IPNetworkAddress::ptr addr = NetworkAddress::SearchForAnyIPNetworkAddress(m_host);
        if(!addr) {
            YHCHAOS_LOG_ERROR(g_logger) << "get addr fail: " << m_host;
            return nullptr;
        }
        addr->setPort(m_port);
        Sock::ptr sock = m_isHttps ? SSLSock::CreateTCP(addr) : Sock::CreateTCP(addr);
        if(!sock) {
            YHCHAOS_LOG_ERROR(g_logger) << "create sock fail: " << *addr;
            return nullptr;
        }
        if(!sock->connect(addr)) {
            YHCHAOS_LOG_ERROR(g_logger) << "sock connect fail: " << *addr;
            return nullptr;
        }

        ptr = new HttpClient(sock);
        ++m_total;
    }
    return HttpClient::ptr(ptr, std::bind(&HttpClientPool::ReleasePtr
                               , std::placeholders::_1, this));
}

void HttpClientPool::ReleasePtr(HttpClient* ptr, HttpClientPool* pool) {
    ++ptr->m_request;
    if(!ptr->isConnected()
            || ((ptr->m_createTime + pool->m_maxAliveTime) <= yhchaos::GetCurrentMS())//目前还处于alive状态
            || (ptr->m_request >= pool->m_maxReq)) {//
        delete ptr;
        --pool->m_total;
        return;
    }
    MtxType::Lock lock(pool->m_mutex);
    pool->m_conns.push_back(ptr);
}

HttpRes::ptr HttpClientPool::doGet(const std::string& url
                          , uint64_t timeout_ms
                          , const std::map<std::string, std::string>& headers
                          , const std::string& body) {
    return doReq(HMethod::GET, url, timeout_ms, headers, body);
}
HttpRes::ptr HttpClientPool::doGet(UriDesc::ptr uri
                                   , uint64_t timeout_ms
                                   , const std::map<std::string, std::string>& headers
                                   , const std::string& body) {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doGet(ss.str(), timeout_ms, headers, body);
}

HttpRes::ptr HttpClientPool::doPost(const std::string& url
                                   , uint64_t timeout_ms
                                   , const std::map<std::string, std::string>& headers
                                   , const std::string& body) {
    return doReq(HMethod::POST, url, timeout_ms, headers, body);
}

HttpRes::ptr HttpClientPool::doPost(UriDesc::ptr uri
                                   , uint64_t timeout_ms
                                   , const std::map<std::string, std::string>& headers
                                   , const std::string& body) {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doPost(ss.str(), timeout_ms, headers, body);
}

HttpRes::ptr HttpClientPool::doReq(HMethod method
                                    , const std::string& url
                                    , uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers
                                    , const std::string& body) {
    HttpReq::ptr req = std::make_shared<HttpReq>();
    req->setPath(url);
    req->setMethod(method);
    //请求的m_close=false
    req->setClose(false);
    //看headers中有没有host
    bool has_host = false;
    for(auto& i : headers) {
        if(strcasecmp(i.first.c_str(), "connection") == 0) {
            if(strcasecmp(i.second.c_str(), "keep-alive") == 0) {
                req->setClose(false);
            }
            continue;
        }

        if(!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
            has_host = !i.second.empty();
        }

        req->setHeader(i.first, i.second);
    }
    //如果headers中没有host，则从m_vhost空，就从m_host中获取，否则从m_vhost中获取
    if(!has_host) {
        if(m_vhost.empty()) {
            req->setHeader("Host", m_host);
        } else {
            req->setHeader("Host", m_vhost);
        }
    }
    req->setBody(body);
    return doReq(req, timeout_ms);
}

HttpRes::ptr HttpClientPool::doReq(HMethod method
                                    , UriDesc::ptr uri
                                    , uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers
                                    , const std::string& body) {
    std::stringstream ss;
    ss << uri->getPath()
       << (uri->getQuery().empty() ? "" : "?")
       << uri->getQuery()
       << (uri->getFragment().empty() ? "" : "#")
       << uri->getFragment();
    return doReq(method, ss.str(), timeout_ms, headers, body);
}

HttpRes::ptr HttpClientPool::doReq(HttpReq::ptr req
                                        , uint64_t timeout_ms) {
    //取出一个连接
    auto conn = getClient();
    if(!conn) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::POOL_GET_CONNECTION
                , nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    auto sock = conn->getSock();
    if(!sock) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::POOL_INVALID_CONNECTION
                , nullptr, "pool host:" + m_host + " port:" + std::to_string(m_port));
    }
    sock->setRecvTimeout(timeout_ms);
    int rt = conn->sendReq(req);
    if(rt == 0) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::SEND_CLOSE_BY_PEER
                , nullptr, "send request closed by peer: " + sock->getRemoteNetworkAddress()->toString());
    }
    if(rt < 0) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::SEND_SOCKET_ERROR
                    , nullptr, "send request socket error errno=" + std::to_string(errno)
                    + " errstr=" + std::string(strerror(errno)));
    }
    auto rsp = conn->recvRsp();
    if(!rsp) {
        return std::make_shared<HttpRes>((int)HttpRes::Error::TIMEOUT
                    , nullptr, "recv response timeout: " + sock->getRemoteNetworkAddress()->toString()
                    + " timeout_ms:" + std::to_string(timeout_ms));
    }
    return std::make_shared<HttpRes>((int)HttpRes::Error::OK, rsp, "ok");
}

}
}
