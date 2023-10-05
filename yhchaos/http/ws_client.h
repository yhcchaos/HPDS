#ifndef __YHCHAOS_HTTP_WS_CLIENT_H__
#define __YHCHAOS_HTTP_WS_CLIENT_H__

#include "yhchaos/http/http_client.h"
#include "yhchaos/http/ws_session.h"

namespace yhchaos {
namespace http {

class WClient : public HttpClient {
public:
    typedef std::shared_ptr<WClient> ptr;
    WClient(Sock::ptr sock, bool owner = true);
    static std::pair<HttpRes::ptr, WClient::ptr> Create(const std::string& url
                                    ,uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers = {});
    /**
     * @brief 创建websocket连接，发送连接验证请求
     * @return 返回HttpRes::ptr（包含请求回应HttpRsp，其中HStatus=HStatus::SWITCHING_PROTOCOLS， 
     * 并设置了Sec-WebSock-Accept头）和WClient::ptr
    */
    static std::pair<HttpRes::ptr, WClient::ptr> Create(UriDesc::ptr uri
                                    ,uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers = {});
    WFrameMSG::ptr recvMesage();

    int32_t sendMSG(WFrameMSG::ptr msg, bool fin = true);
    int32_t sendMSG(const std::string& msg, int32_t opcode = WFrameHead::TEXT_FRAME, bool fin = true);
    int32_t ping();
    int32_t pong();
};

}
}

#endif
