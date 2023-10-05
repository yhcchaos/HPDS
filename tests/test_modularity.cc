#include "yhchaos/modularity.h"
#include "yhchaos/singleton.h"
#include <iostream>
#include "yhchaos/log.h"
#include "yhchaos/db/cpp_redis.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

class A {
public:
    A() {
        std::cout << "A::A " << this << std::endl;
    }

    ~A() {
        std::cout << "A::~A " << this << std::endl;
    }

};

class MyModularity : public yhchaos::DPModularity {
public:
    MyModularity()
        :DPModularity("hello", "1.0", "") {
        //yhchaos::Singleton<A>::GetInstance();
    }

    bool onResolve() override {
        yhchaos::Singleton<A>::GetInstance();
        std::cout << "-----------onResolve------------" << std::endl;
        return true;
    }

    bool onUnload() override {
        yhchaos::Singleton<A>::GetInstance();
        std::cout << "-----------onUnload------------" << std::endl;
        return true;
    }

    bool onSvrReady() {
        registerService("dp", "yhchaos.top", "blog");
        auto rpy = yhchaos::CppRedisUtil::Cmd("local", "get abc");
        if(!rpy) {
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd get abc error";
        } else {
            YHCHAOS_LOG_ERROR(g_logger) << "redis get abc: "
                << (rpy->str ? rpy->str : "(null)");
        }
        return true;
    }

    bool handleDPReq(yhchaos::DPReq::ptr request
                        ,yhchaos::DPRsp::ptr response
                        ,yhchaos::DPStream::ptr stream) {
        //YHCHAOS_LOG_INFO(g_logger) << "handleDPReq " << request->toString();
        //sleep(1);
        response->setRes(0);
        response->setResStr("ok");
        response->setBody("echo: " + request->getBody());

        usleep(100 * 1000);
        auto addr = stream->getLocalNetworkAddressString();
        if(addr.find("8061") != std::string::npos) {
            if(rand() % 100 < 50) {
                usleep(10 * 1000);
            } else if(rand() % 100 < 10) {
                response->setRes(-1000);
            }
        } else {
            //if(rand() % 100 < 25) {
            //    usleep(10 * 1000);
            //} else if(rand() % 100 < 10) {
            //    response->setRes(-1000);
            //}
        }
        return true;
        //return rand() % 100 < 90;
    }

    bool handleDPNotify(yhchaos::DPNotify::ptr notify 
                        ,yhchaos::DPStream::ptr stream) {
        YHCHAOS_LOG_INFO(g_logger) << "handleDPNotify " << notify->toString();
        return true;
    }

};

extern "C" {

yhchaos::Modularity* CreateModularity() {
    yhchaos::Singleton<A>::GetInstance();
    std::cout << "=============CreateModularity=================" << std::endl;
    return new MyModularity;
}

void DestoryModularity(yhchaos::Modularity* ptr) {
    std::cout << "=============DestoryModularity=================" << std::endl;
    delete ptr;
}

}
