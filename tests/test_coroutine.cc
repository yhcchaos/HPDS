#include "yhchaos/yhchaos.h"

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void run_in_coroutine() {
    YHCHAOS_LOG_INFO(g_logger) << "run_in_coroutine begin";
    yhchaos::Coroutine::YieldToHold();
    YHCHAOS_LOG_INFO(g_logger) << "run_in_coroutine end";
    yhchaos::Coroutine::YieldToHold();
}

void test_coroutine() {
    YHCHAOS_LOG_INFO(g_logger) << "main begin -1";
    {
        yhchaos::Coroutine::GetThis();
        YHCHAOS_LOG_INFO(g_logger) << "main begin";
        yhchaos::Coroutine::ptr coroutine(new yhchaos::Coroutine(run_in_coroutine));
        coroutine->swapIn();
        YHCHAOS_LOG_INFO(g_logger) << "main after swapIn";
        coroutine->swapIn();
        YHCHAOS_LOG_INFO(g_logger) << "main after end";
        coroutine->swapIn();
    }
    YHCHAOS_LOG_INFO(g_logger) << "main after end2";
}

int main(int argc, char** argv) {
    yhchaos::CppThread::SetName("main");

    std::vector<yhchaos::CppThread::ptr> thrs;
    for(int i = 0; i < 3; ++i) {
        thrs.push_back(yhchaos::CppThread::ptr(
                    new yhchaos::CppThread(&test_coroutine, "name_" + std::to_string(i))));
    }
    for(auto i : thrs) {
        i->join();
    }
    return 0;
}
