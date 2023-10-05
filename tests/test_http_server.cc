#include "yhchaos/http/httpsvr.h"
#include "yhchaos/log.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

#define XX(...) #__VA_ARGS__


yhchaos::IOCoScheduler::ptr worker;
void run() {
    g_logger->setLevel(yhchaos::LogLevel::INFO);
    //yhchaos::http::HttpSvr::ptr server(new yhchaos::http::HttpSvr(true, worker.get(), yhchaos::IOCoScheduler::GetThis()));
    yhchaos::http::HttpSvr::ptr server(new yhchaos::http::HttpSvr(true));
    yhchaos::NetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress("0.0.0.0:8020");
    while(!server->bind(addr)) {
        sleep(2);
    }
    auto sd = server->getCppServletDispatch();
    sd->addCppServlet("/yhchaos/xx", [](yhchaos::http::HttpReq::ptr req
                ,yhchaos::http::HttpRsp::ptr rsp
                ,yhchaos::http::HSession::ptr session) {
            rsp->setBody(req->toString());
            return 0;
    });

    sd->addGlobCppServlet("/yhchaos/*", [](yhchaos::http::HttpReq::ptr req
                ,yhchaos::http::HttpRsp::ptr rsp
                ,yhchaos::http::HSession::ptr session) {
            rsp->setBody("Glob:\r\n" + req->toString());
            return 0;
    });

    sd->addGlobCppServlet("/yhchaosx/*", [](yhchaos::http::HttpReq::ptr req
                ,yhchaos::http::HttpRsp::ptr rsp
                ,yhchaos::http::HSession::ptr session) {
            rsp->setBody(XX(<html>
<head><title>404 Not Found</title></head>
<body>
<center><h1>404 Not Found</h1></center>
<hr><center>nginx/1.16.0</center>
</body>
</html>
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
));
            return 0;
    });

    server->start();
}

int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom(1, true, "main");
    worker.reset(new yhchaos::IOCoScheduler(3, false, "worker"));
    iom.coschedule(run);
    return 0;
}
