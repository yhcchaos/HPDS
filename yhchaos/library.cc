#include "library.h"

#include <dlfcn.h>
#include "yhchaos/appconfig.h"
#include "yhchaos/environment.h"
#include "yhchaos/log.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

typedef Modularity* (*create_module)();
typedef void (*destory_module)(Modularity*);

class ModularityCloser {
public:
    ModularityCloser(void* handle, destory_module d)
        :m_handle(handle)
        ,m_destory(d) {
    }

    void operator()(Modularity* module) {
        std::string name = module->getName();
        std::string version = module->getVersion();
        std::string path = module->getFilename();
        m_destory(module);
        int rt = dlclose(m_handle);
        if(rt) {
            YHCHAOS_LOG_ERROR(g_logger) << "dlclose handle fail handle="
                << m_handle << " name=" << name
                << " version=" << version
                << " path=" << path
                << " error=" << dlerror();
        } else {
            YHCHAOS_LOG_INFO(g_logger) << "destory module=" << name
                << " version=" << version
                << " path=" << path
                << " handle=" << m_handle
                << " success";
        }
    }
private:
    void* m_handle;
    destory_module m_destory;
};

Modularity::ptr Library::GetModularity(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if(!handle) {
        YHCHAOS_LOG_ERROR(g_logger) << "cannot load library path="
            << path << " error=" << dlerror();
        return nullptr;
    }

    create_module create = (create_module)dlsym(handle, "CreateModularity");
    if(!create) {
        YHCHAOS_LOG_ERROR(g_logger) << "cannot load symbol CreateModularity in "
            << path << " error=" << dlerror();
        dlclose(handle);
        return nullptr;
    }

    destory_module destory = (destory_module)dlsym(handle, "DestoryModularity");
    if(!destory) {
        YHCHAOS_LOG_ERROR(g_logger) << "cannot load symbol DestoryModularity in "
            << path << " error=" << dlerror();
        dlclose(handle);
        return nullptr;
    }

    Modularity::ptr module(create(), ModularityCloser(handle, destory));
    module->setFilename(path);
    YHCHAOS_LOG_INFO(g_logger) << "load module name=" << module->getName()
        << " version=" << module->getVersion()
        << " path=" << module->getFilename()
        << " success";
    //强制重新加载一遍配置文件
    AppConfig::ResolveFromConfDir(yhchaos::EnvironmentMgr::GetInstance()->getAppConfigPath(), true);
    return module;
}

}
