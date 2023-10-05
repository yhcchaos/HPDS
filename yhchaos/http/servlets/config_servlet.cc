#include "config_cpp_servlet.h"
#include "yhchaos/appconfig.h"

namespace yhchaos {
namespace http {

AppConfigCppServlet::AppConfigCppServlet()
    :CppServlet("AppConfigCppServlet") {
}

int32_t AppConfigCppServlet::handle(yhchaos::http::HttpReq::ptr request
                              ,yhchaos::http::HttpRsp::ptr response
                              ,yhchaos::http::HSession::ptr session) {
    std::string type = request->getParam("type");
    if(type == "json") {
        response->setHeader("Content-Type", "text/json charset=utf-8");
    } else {
        response->setHeader("Content-Type", "text/yaml charset=utf-8");
    }
    YAML::Node node;
    yhchaos::AppConfig::Visit([&node](AppConfigVarBase::ptr base) {
        YAML::Node n;
        try {
            n = YAML::Resolve(base->toString());
        } catch(...) {
            return;
        }
        node[base->getName()] = n;
        node[base->getName() + "$description"] = base->getDescription();
    });
    if(type == "json") {
        Json::Value jvalue;
        if(YamlToJson(node, jvalue)) {
            response->setBody(JsonUtil::ToString(jvalue));
            return 0;
        }
    }
    std::stringstream ss;
    ss << node;
    response->setBody(ss.str());
    return 0;
}

}
}
