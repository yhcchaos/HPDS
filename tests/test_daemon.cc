#include "yhchaos/daemon.h"
#include "yhchaos/iocoscheduler.h"
#include "yhchaos/log.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

yhchaos::TimedCoroutine::ptr timer;
int server_main(int argc, char** argv) {
    YHCHAOS_LOG_INFO(g_logger) << yhchaos::ProcessInfoMgr::GetInstance()->toString();
    yhchaos::IOCoScheduler iom(1);
    timer = iom.addTimedCoroutine(1000, [](){
            YHCHAOS_LOG_INFO(g_logger) << "onTimedCoroutine";
            static int count = 0;
            if(++count > 10) {
                exit(1);
            }
    }, true);
    return 0;
}

int main(int argc, char** argv) {
    return yhchaos::start_daemon(argc, argv, server_main, argc != 1);
}
