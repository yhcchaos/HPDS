#include "yhchaos/yhchaos.h"
#include <unistd.h>

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

int count = 0;
//yhchaos::RWMtx s_mutex;
yhchaos::Mtx s_mutex;

void fun1() {
    YHCHAOS_LOG_INFO(g_logger) << "name: " << yhchaos::CppThread::GetName()
                             << " this.name: " << yhchaos::CppThread::GetThis()->getName()
                             << " id: " << yhchaos::GetCppThreadId()
                             << " this.id: " << yhchaos::CppThread::GetThis()->getId();

    for(int i = 0; i < 100000; ++i) {
        //yhchaos::RWMtx::WriteLock lock(s_mutex);
        yhchaos::Mtx::Lock lock(s_mutex);
        ++count;
    }
}

void fun2() {
    while(true) {
        YHCHAOS_LOG_INFO(g_logger) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}

void fun3() {
    while(true) {
        YHCHAOS_LOG_INFO(g_logger) << "========================================";
    }
}

int main(int argc, char** argv) {
    YHCHAOS_LOG_INFO(g_logger) << "thread test begin";
    YAML::Node root = YAML::ResolveFile("/home/yhchaos/test/yhchaos/bin/conf/log2.yml");
    yhchaos::AppConfig::ResolveFromYaml(root);

    std::vector<yhchaos::CppThread::ptr> thrs;
    for(int i = 0; i < 1; ++i) {
        yhchaos::CppThread::ptr thr(new yhchaos::CppThread(&fun2, "name_" + std::to_string(i * 2)));
        //yhchaos::CppThread::ptr thr2(new yhchaos::CppThread(&fun3, "name_" + std::to_string(i * 2 + 1)));
        thrs.push_back(thr);
        //thrs.push_back(thr2);
    }

    for(size_t i = 0; i < thrs.size(); ++i) {
        thrs[i]->join();
    }
    YHCHAOS_LOG_INFO(g_logger) << "thread test end";
    YHCHAOS_LOG_INFO(g_logger) << "count=" << count;

    return 0;
}
