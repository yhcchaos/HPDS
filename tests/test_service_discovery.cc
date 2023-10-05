#include "yhchaos/streams/service_discovery.h"
#include "yhchaos/iocoscheduler.h"
#include "yhchaos/dp/dp_stream.h"
#include "yhchaos/log.h"
#include "yhchaos/worker.h"

yhchaos::ZKSD::ptr zksd(new yhchaos::ZKSD("127.0.0.1:21812"));
yhchaos::DPSDLoadBalance::ptr rsdlb(new yhchaos::DPSDLoadBalance(zksd));

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

std::atomic<uint32_t> s_id;
void on_timer() {
    g_logger->setLevel(yhchaos::LogLevel::INFO);
    //YHCHAOS_LOG_INFO(g_logger) << "on_timer";
    yhchaos::DPReq::ptr req(new yhchaos::DPReq);
    req->setSn(++s_id);
    req->setCmd(100);
    req->setBody("hello");

    auto rt = rsdlb->request("yhchaos.top", "blog", req, 1000);
    if(!rt->response) {
        if(req->getSn() % 50 == 0) {
            YHCHAOS_LOG_ERROR(g_logger) << "invalid response: " << rt->toString();
        }
    } else {
        if(req->getSn() % 1000 == 0) {
            YHCHAOS_LOG_INFO(g_logger) << rt->toString();
        }
    }
}

void run() {
    zksd->setSelfInfo("127.0.0.1:2222");
    zksd->setSelfData("aaaa");

    std::unordered_map<std::string, std::unordered_map<std::string,std::string> > confs;
    confs["yhchaos.top"]["blog"] = "fair";
    rsdlb->start(confs);
    //YHCHAOS_LOG_INFO(g_logger) << "on_timer---";

    yhchaos::IOCoScheduler::GetThis()->addTimedCoroutine(1, on_timer, true);
}

int main(int argc, char** argv) {
    yhchaos::CoSchedulerMgr::GetInstance()->init({
        {"service_io", {
            {"thread_num", "1"}
        }}
    });
    yhchaos::IOCoScheduler iom(1);
    iom.addTimedCoroutine(1000, [](){
            std::cout << rsdlb->statusString() << std::endl;
    }, true);
    iom.coschedule(run);
    return 0;
}
