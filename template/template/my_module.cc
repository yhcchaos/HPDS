#include "my_modularity.h"
#include "yhchaos/appconfig.h"
#include "yhchaos/log.h"

namespace name_space {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

MyModularity::MyModularity()
    //name version filename
    :yhchaos::Modularity("project_name", "1.0", "") {
}

bool MyModularity::onResolve() {
    YHCHAOS_LOG_INFO(g_logger) << "onResolve";
    return true;
}

bool MyModularity::onUnload() {
    YHCHAOS_LOG_INFO(g_logger) << "onUnload";
    return true;
}

bool MyModularity::onSvrReady() {
    YHCHAOS_LOG_INFO(g_logger) << "onSvrReady";
    return true;
}

bool MyModularity::onSvrUp() {
    YHCHAOS_LOG_INFO(g_logger) << "onSvrUp";
    return true;
}

}

extern "C" {

yhchaos::Modularity* CreateModularity() {
    yhchaos::Modularity* module = new name_space::MyModularity;
    YHCHAOS_LOG_INFO(name_space::g_logger) << "CreateModularity " << module;
    return module;
}

void DestoryModularity(yhchaos::Modularity* module) {
    YHCHAOS_LOG_INFO(name_space::g_logger) << "CreateModularity " << module;
    delete module;
}

}
