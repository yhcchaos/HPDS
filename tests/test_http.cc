#include "yhchaos/http/http.h"
#include "yhchaos/log.h"

void test_request() {
    yhchaos::http::HttpReq::ptr req(new yhchaos::http::HttpReq);
    req->setHeader("host" , "www.yhchaos.top");
    req->setBody("hello yhchaos");
    req->dump(std::cout) << std::endl;
}

void test_response() {
    yhchaos::http::HttpRsp::ptr rsp(new yhchaos::http::HttpRsp);
    rsp->setHeader("X-X", "yhchaos");
    rsp->setBody("hello yhchaos");
    rsp->setStatus((yhchaos::http::HStatus)400);
    rsp->setClose(false);

    rsp->dump(std::cout) << std::endl;
}

int main(int argc, char** argv) {
    test_request();
    test_response();
    return 0;
}
