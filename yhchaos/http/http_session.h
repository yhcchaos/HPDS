#ifndef __YHCHAOS_HTTP_HTTP_SESSION_H__
#define __YHCHAOS_HTTP_HTTP_SESSION_H__

#include "yhchaos/streams/sock_stream.h"
#include "http.h"

namespace yhchaos {
namespace http {

/**
 * @brief HTTPSession封装，对某个socket解析http请求报文，发送http响应报文，
 */
class HSession : public SockStream {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<HSession> ptr;

    /**
     * @brief 构造函数
     * @param[in] sock Sock类型
     * @param[in] owner 是否托管
     */
    HSession(Sock::ptr sock, bool owner = true);

    /**
     * @brief 接收HTTP请求，read循环读socket，并将读出的http报文解析为httpReq对象
     */
    HttpReq::ptr recvReq();

    /**
     * @brief 发送HTTP响应
     * @param[in] rsp HTTP响应
     * @return >0 发送成功
     *         =0 对方关闭
     *         <0 Sock异常
     */
    int sendRsp(HttpRsp::ptr rsp);
};

}
}

#endif
