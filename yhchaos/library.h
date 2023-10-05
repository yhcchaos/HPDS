#ifndef __YHCHAOS_LIBRARY_H__
#define __YHCHAOS_LIBRARY_H__

#include <memory>
#include "modularity.h"

namespace yhchaos {

class Library {
public:
    static Modularity::ptr GetModularity(const std::string& path);
};

}

#endif
