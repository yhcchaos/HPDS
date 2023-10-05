#include "yhchaos/http/ws_client.h"
#include "yhchaos/iocoscheduler.h"
#include "yhchaos/util.h"

void run() {
    auto rt = yhchaos::http::WClient::Create("http://127.0.0.1:8020/yhchaos", 1000);
    if(!rt.second) {
        std::cout << rt.first->toString() << std::endl;
        return;
    }

    auto conn = rt.second;
    while(true) {
        //for(int i = 0; i < 1100; ++i) {
        for(int i = 0; i < 1; ++i) {
            conn->sendMSG(yhchaos::random_string(60), yhchaos::http::WFrameHead::TEXT_FRAME, false);
        }
        conn->sendMSG(yhchaos::random_string(65), yhchaos::http::WFrameHead::TEXT_FRAME, true);
        auto msg = conn->recvMSG();
        if(!msg) {
            break;
        }
        std::cout << "opcode=" << msg->getOpcode()
                  << " data=" << msg->getData() << std::endl;

        sleep(10);
    }
}

int main(int argc, char** argv) {
    srand(time(0));
    yhchaos::IOCoScheduler iom(1);
    iom.coschedule(run);
    return 0;
}
