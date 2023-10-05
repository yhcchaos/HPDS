#ifndef __YHCHAOS_DP_DP_SERVER_H__
#define __YHCHAOS_DP_DP_SERVER_H__

#include "yhchaos/dp/dp_stream.h"
#include "yhchaos/tcpserver.h"

namespace yhchaos {
//dp这种命名是为了强调该框架或组件的稳定性、可靠性或性能,
//"DP" 可能传达了项目坚固、可靠或稳定的意象，这与网络通信或
//分布式系统中的稳定性和可靠性相关。
class DPSvr : public TcpSvr {
public:
    typedef std::shared_ptr<DPSvr> ptr;
    DPSvr(const std::string& type = "dp"
               ,yhchaos::IOCoScheduler* worker = yhchaos::IOCoScheduler::GetThis()
               ,yhchaos::IOCoScheduler* io_worker = yhchaos::IOCoScheduler::GetThis()
               ,yhchaos::IOCoScheduler* accept_worker = yhchaos::IOCoScheduler::GetThis());

protected:
    virtual void handleClient(Sock::ptr client) override;
};

}

#endif
