#include "yhchaos/environment.h"
#include <unistd.h>
#include <iostream>
#include <fstream>

struct A {
    A() {
        std::ifstream ifs("/proc/" + std::to_string(getpid()) + "/cmdline", std::ios::binary);
        std::string content;
        content.resize(4096);

        ifs.read(&content[0], content.size());
        content.resize(ifs.gcount());

        for(size_t i = 0; i < content.size(); ++i) {
            std::cout << i << " - " << content[i] << " - " << (int)content[i] << std::endl;
        }
    }
};

A a;

int main(int argc, char** argv) {
    std::cout << "argc=" << argc << std::endl;
    yhchaos::EnvironmentMgr::GetInstance()->addHelp("s", "start with the terminal");
    yhchaos::EnvironmentMgr::GetInstance()->addHelp("d", "run as daemon");
    yhchaos::EnvironmentMgr::GetInstance()->addHelp("p", "print help");
    if(!yhchaos::EnvironmentMgr::GetInstance()->init(argc, argv)) {
        yhchaos::EnvironmentMgr::GetInstance()->printHelp();
        return 0;
    }

    std::cout << "exe=" << yhchaos::EnvironmentMgr::GetInstance()->getExe() << std::endl;
    std::cout << "cwd=" << yhchaos::EnvironmentMgr::GetInstance()->getCwd() << std::endl;

    std::cout << "path=" << yhchaos::EnvironmentMgr::GetInstance()->getEnvironment("PATH", "xxx") << std::endl;
    std::cout << "test=" << yhchaos::EnvironmentMgr::GetInstance()->getEnvironment("TEST", "") << std::endl;
    std::cout << "set env " << yhchaos::EnvironmentMgr::GetInstance()->setEnvironment("TEST", "yy") << std::endl;
    std::cout << "test=" << yhchaos::EnvironmentMgr::GetInstance()->getEnvironment("TEST", "") << std::endl;
    if(yhchaos::EnvironmentMgr::GetInstance()->has("p")) {
        yhchaos::EnvironmentMgr::GetInstance()->printHelp();
    }
    return 0;
}
