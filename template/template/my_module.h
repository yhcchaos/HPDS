#include "yhchaos/modularity.h"

namespace name_space {

class MyModularity : public yhchaos::Modularity {
public:
    typedef std::shared_ptr<MyModularity> ptr;
    MyModularity();
    bool onResolve() override;
    bool onUnload() override;
    bool onSvrReady() override;
    bool onSvrUp() override;
};

}
