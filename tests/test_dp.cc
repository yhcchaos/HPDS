#include "yhchaos/yhchaos.h"
#include "yhchaos/dp/dp_stream.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

yhchaos::DPClient::ptr conn(new yhchaos::DPClient);
void run() {
    conn->setAutoConnect(true);
    yhchaos::NetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAny("127.0.0.1:8061");
    if(!conn->connect(addr)) {
        YHCHAOS_LOG_INFO(g_logger) << "connect " << *addr << " false";
    }
    conn->start();

    yhchaos::IOCoScheduler::GetThis()->addTimedCoroutine(1000, [](){
        yhchaos::DPReq::ptr req(new yhchaos::DPReq);
        static uint32_t s_sn = 0;
        req->setSn(++s_sn);
        req->setCmd(100);
        req->setBody("hello world sn=" + std::to_string(s_sn));

        auto rsp = conn->request(req, 300);
        if(rsp->response) {
            YHCHAOS_LOG_INFO(g_logger) << rsp->response->toString();
        } else {
            YHCHAOS_LOG_INFO(g_logger) << "error result=" << rsp->result;
        }
    }, true);
}

int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom(1);
    iom.coschedule(run);
    return 0;
}
