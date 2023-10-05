#include "http.h"
#include "yhchaos/util.h"

namespace yhchaos {
namespace http {

HMethod StringToHMethod(const std::string& m) {
#define XX(num, name, string) \
    if(strcmp(#string, m.c_str()) == 0) { \
        return HMethod::name; \
    }
    METHOD_MAP(XX);
#undef XX
    return HMethod::INVALID_METHOD;
}

HMethod CharsToHMethod(const char* m) {
#define XX(num, name, string) \
    if(strncmp(#string, m, strlen(#string)) == 0) { \
        return HMethod::name; \
    }
    METHOD_MAP(XX);
#undef XX
    return HMethod::INVALID_METHOD;
}

static const char* s_method_string[] = {
#define XX(num, name, string) #string,
    METHOD_MAP(XX)
#undef XX
};

const char* HMethodToString(const HMethod& m) {
    uint32_t idx = (uint32_t)m;
    if(idx >= (sizeof(s_method_string) / sizeof(s_method_string[0]))) {
        return "<unknown>";
    }
    return s_method_string[idx];
}

const char* HStatusToString(const HStatus& s) {
    switch(s) {
#define XX(code, name, msg) \
        case HStatus::name: \
            return #msg;
        STATUS_MAP(XX);
#undef XX
        default:
            return "<unknown>";
    }
}

bool CaseInsensitiveLess::operator()(const std::string& lhs
                            ,const std::string& rhs) const {
    return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
}

HttpReq::HttpReq(uint8_t version, bool close)
    :m_method(HMethod::GET)
    ,m_version(version)
    ,m_close(close)
    ,m_websocket(false)
    ,m_parserParamFlag(0)
    ,m_path("/") {
}

std::string HttpReq::getHeader(const std::string& key
                            ,const std::string& def) const {
    auto it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

std::shared_ptr<HttpRsp> HttpReq::createRsp() {
    HttpRsp::ptr rsp(new HttpRsp(getVersion()
                            ,isClose()));
    return rsp;
}

std::string HttpReq::getParam(const std::string& key
                            ,const std::string& def) {
    initQueryParam();
    initBodyParam();
    auto it = m_params.find(key);
    return it == m_params.end() ? def : it->second;
}

std::string HttpReq::getCookie(const std::string& key
                            ,const std::string& def) {
    initCookies();
    auto it = m_cookies.find(key);
    return it == m_cookies.end() ? def : it->second;
}

void HttpReq::setHeader(const std::string& key, const std::string& val) {
    m_headers[key] = val;
}

void HttpReq::setParam(const std::string& key, const std::string& val) {
    m_params[key] = val;
}

void HttpReq::setCookie(const std::string& key, const std::string& val) {
    m_cookies[key] = val;
}

void HttpReq::delHeader(const std::string& key) {
    m_headers.erase(key);
}

void HttpReq::delParam(const std::string& key) {
    m_params.erase(key);
}

void HttpReq::delCookie(const std::string& key) {
    m_cookies.erase(key);
}

bool HttpReq::hasHeader(const std::string& key, std::string* val) {
    auto it = m_headers.find(key);
    if(it == m_headers.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

bool HttpReq::hasParam(const std::string& key, std::string* val) {
    initQueryParam();
    initBodyParam();
    auto it = m_params.find(key);
    if(it == m_params.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

bool HttpReq::hasCookie(const std::string& key, std::string* val) {
    initCookies();
    auto it = m_cookies.find(key);
    if(it == m_cookies.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

std::string HttpReq::toString() const {
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

std::ostream& HttpReq::dump(std::ostream& os) const {
    //GET /uri HTTP/1.1
    //Host: wwww.yhchaos.top
    //
    //
    os << HMethodToString(m_method) << " "
       << m_path
       << (m_query.empty() ? "" : "?")
       << m_query
       << (m_fragment.empty() ? "" : "#")
       << m_fragment
       << " HTTP/"
       << ((uint32_t)(m_version >> 4))
       << "."
       << ((uint32_t)(m_version & 0x0F))
       << "\r\n";
    if(!m_websocket) {
        os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
    }
    for(auto& i : m_headers) {
        if(!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }

    if(!m_body.empty()) {
        os << "content-length: " << m_body.size() << "\r\n\r\n"
           << m_body;
    } else {
        os << "\r\n";
    }
    return os;
}

void HttpReq::init() {
    std::string conn = getHeader("connection");
    if(!conn.empty()) {
        if(strcasecmp(conn.c_str(), "keep-alive") == 0) {
            m_close = false;
        } else {
            m_close = true;
        }
    }
}

void HttpReq::initParam() {
    initQueryParam();
    initBodyParam();
    initCookies();
}

void HttpReq::initQueryParam() {
    if(m_parserParamFlag & 0x1) {
        return;
    }

#define PARSE_PARAM(str, m, flag, trim) \
    size_t pos = 0; \
    do { \
        //找到第一个等号的位置=key
        size_t last = pos; \
        pos = str.find('=', pos); \
        if(pos == std::string::npos) { \
            break; \
        } \
        //找到第一个=号后面第一个&的位置=pos
        //last=pos+1
        size_t key = pos; \
        pos = str.find(flag, pos); \
        m.insert(std::make_pair(trim(str.substr(last, key - last)), \
                    yhchaos::StringUtil::UrlDecode(str.substr(key + 1, pos - key - 1)))); \
        if(pos == std::string::npos) { \
            break; \
        } \
        ++pos; \
    } while(true);
    //从m_query中解析出m_params
    PARSE_PARAM(m_query, m_params, '&',);
    m_parserParamFlag |= 0x1;
}

void HttpReq::initBodyParam() {
    if(m_parserParamFlag & 0x2) {
        return;
    }
    std::string content_type = getHeader("content-type");
    //将表单数据编码为URL参数的一种方式
    if(strcasestr(content_type.c_str(), "application/x-www-form-urlencoded") == nullptr) {
        m_parserParamFlag |= 0x2;
        return;
    }
    PARSE_PARAM(m_body, m_params, '&',);
    m_parserParamFlag |= 0x2;
}

void HttpReq::initCookies() {
    if(m_parserParamFlag & 0x4) {
        return;
    }
    std::string cookie = getHeader("cookie");
    if(cookie.empty()) {
        m_parserParamFlag |= 0x4;
        return;
    }
    //Trim删除字符串首尾的\t\r\n
    PARSE_PARAM(cookie, m_cookies, ';', yhchaos::StringUtil::Trim);
    m_parserParamFlag |= 0x4;
}


HttpRsp::HttpRsp(uint8_t version, bool close)
    :m_status(HStatus::OK)
    ,m_version(version)
    ,m_close(close)
    ,m_websocket(false) {
}

std::string HttpRsp::getHeader(const std::string& key, const std::string& def) const {
    auto it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

void HttpRsp::setHeader(const std::string& key, const std::string& val) {
    m_headers[key] = val;
}

void HttpRsp::delHeader(const std::string& key) {
    m_headers.erase(key);
}

void HttpRsp::setRedirect(const std::string& uri) {
    m_status = HStatus::FOUND;
    setHeader("Location", uri);
}

void HttpRsp::setCookie(const std::string& key, const std::string& val,
                             time_t expired, const std::string& path,
                             const std::string& domain, bool secure) {
    std::stringstream ss;
    ss << key << "=" << val;
    if(expired > 0) {
        ss << ";expires=" << yhchaos::Time2Str(expired, "%a, %d %b %Y %H:%M:%S") << " GMT";
    }
    if(!domain.empty()) {
        ss << ";domain=" << domain;
    }
    if(!path.empty()) {
        ss << ";path=" << path;
    }
    if(secure) {
        ss << ";secure";
    }
    m_cookies.push_back(ss.str());
}


std::string HttpRsp::toString() const {
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

std::ostream& HttpRsp::dump(std::ostream& os) const {
    os << "HTTP/"
       << ((uint32_t)(m_version >> 4))
       << "."
       << ((uint32_t)(m_version & 0x0F))
       << " "
       << (uint32_t)m_status
       << " "
       << (m_reason.empty() ? HStatusToString(m_status) : m_reason)
       << "\r\n";

    for(auto& i : m_headers) {
        if(!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }
    for(auto& i : m_cookies) {
        os << "Set-Cookie: " << i << "\r\n";
    }
    if(!m_websocket) {
        os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
    }
    if(!m_body.empty()) {
        os << "content-length: " << m_body.size() << "\r\n\r\n"
           << m_body;
    } else {
        os << "\r\n";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const HttpReq& req) {
    return req.dump(os);
}

std::ostream& operator<<(std::ostream& os, const HttpRsp& rsp) {
    return rsp.dump(os);
}

}
}
