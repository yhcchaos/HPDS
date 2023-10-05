#include "yhchaos/yhchaos.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void test_coroutine() {
    static int s_count = 5;
    YHCHAOS_LOG_INFO(g_logger) << "test in coroutine s_count=" << s_count;

    sleep(1);
    if(--s_count >= 0) {
        yhchaos::CoScheduler::GetThis()->coschedule(&test_coroutine, yhchaos::GetCppThreadId());
    }
}

int main(int argc, char** argv) {
    YHCHAOS_LOG_INFO(g_logger) << "main";
    yhchaos::CoScheduler sc(3, false, "test");
    sc.start();
    sleep(2);
    YHCHAOS_LOG_INFO(g_logger) << "coschedule";
    sc.coschedule(&test_coroutine);
    sc.stop();
    YHCHAOS_LOG_INFO(g_logger) << "over";
    return 0;
}
