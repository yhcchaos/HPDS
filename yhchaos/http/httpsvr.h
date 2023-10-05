#ifndef __YHCHAOS_HTTP_HTTP_SERVER_H__
#define __YHCHAOS_HTTP_HTTP_SERVER_H__

#include "yhchaos/tcpserver.h"
#include "http_session.h"
#include "cpp_servlet.h"

namespace yhchaos {
namespace http {

/**
 * @brief HTTP服务器类
 */
class HttpSvr : public TcpSvr {
public:
    /// 智能指针类型
    typedef std::shared_ptr<HttpSvr> ptr;

    /**
     * @brief 构造函数
     * @param[in] keepalive 是否长连接
     * @param[in] worker 工作调度器
     * @param[in] accept_worker 接收连接调度器
     */
    HttpSvr(bool keepalive = false
               ,yhchaos::IOCoScheduler* worker = yhchaos::IOCoScheduler::GetThis()
               ,yhchaos::IOCoScheduler* io_worker = yhchaos::IOCoScheduler::GetThis()
               ,yhchaos::IOCoScheduler* accept_worker = yhchaos::IOCoScheduler::GetThis());

    /**
     * @brief 获取CppServletDispatch
     */
    CppServletDispatch::ptr getCppServletDispatch() const { return m_dispatch;}

    /**
     * @brief 设置CppServletDispatch
     */
    void setCppServletDispatch(CppServletDispatch::ptr v) { m_dispatch = v;}

    virtual void setName(const std::string& v) override;
protected:
    virtual void handleClient(Sock::ptr client) override;
private:
    /// 是否支持长连接，一条连接可以支持多个请求和相应的发送
    bool m_isKeepalive;//keepalive=false
    /// CppServlet分发器
    CppServletDispatch::ptr m_dispatch;//m_dispatch.reset(new CppServletDispatch);
    //TcpSvr的m_type = "http";
};

}
}

#endif
