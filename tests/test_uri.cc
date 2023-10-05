#include "yhchaos/uridesc.h"
#include <iostream>

int main(int argc, char** argv) {
    //yhchaos::UriDesc::ptr uri = yhchaos::UriDesc::Create("http://www.yhchaos.top/test/uri?id=100&name=yhchaos#frg");
    //yhchaos::UriDesc::ptr uri = yhchaos::UriDesc::Create("http://admin@www.yhchaos.top/test/中文/uri?id=100&name=yhchaos&vv=中文#frg中文");
    yhchaos::UriDesc::ptr uri = yhchaos::UriDesc::Create("http://admin@www.yhchaos.top");
    //yhchaos::UriDesc::ptr uri = yhchaos::UriDesc::Create("http://www.yhchaos.top/test/uri");
    std::cout << uri->toString() << std::endl;
    auto addr = uri->createNetworkAddress();
    std::cout << *addr << std::endl;
    return 0;
}
