#ifndef __YHCHAOS_HTTP_SERVLETS_CONFIG_SERVLET_H__
#define __YHCHAOS_HTTP_SERVLETS_CONFIG_SERVLET_H__

#include "yhchaos/http/cpp_servlet.h"

namespace yhchaos {
namespace http {

class AppConfigCppServlet : public CppServlet {
public:
    AppConfigCppServlet();
    virtual int32_t handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) override;
};

}
}

#endif
