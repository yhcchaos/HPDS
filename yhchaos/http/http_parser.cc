#include "http_parser.h"
#include "yhchaos/log.h"
#include "yhchaos/appconfig.h"
#include <string.h>

namespace yhchaos {
namespace http {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

static yhchaos::AppConfigVar<uint64_t>::ptr g_http_request_buffer_size =
    yhchaos::AppConfig::SearchFor("http.request.buffer_size"
                ,(uint64_t)(4 * 1024), "http request buffer size");

static yhchaos::AppConfigVar<uint64_t>::ptr g_http_request_max_body_size =
    yhchaos::AppConfig::SearchFor("http.request.max_body_size"
                ,(uint64_t)(64 * 1024 * 1024), "http request max body size");

static yhchaos::AppConfigVar<uint64_t>::ptr g_http_response_buffer_size =
    yhchaos::AppConfig::SearchFor("http.response.buffer_size"
                ,(uint64_t)(4 * 1024), "http response buffer size");

static yhchaos::AppConfigVar<uint64_t>::ptr g_http_response_max_body_size =
    yhchaos::AppConfig::SearchFor("http.response.max_body_size"
                ,(uint64_t)(64 * 1024 * 1024), "http response max body size");

static uint64_t s_http_request_buffer_size = 0;
static uint64_t s_http_request_max_body_size = 0;
static uint64_t s_http_response_buffer_size = 0;
static uint64_t s_http_response_max_body_size = 0;

uint64_t HttpReqParser::GetHttpReqBufferSize() {
    return s_http_request_buffer_size;
}

uint64_t HttpReqParser::GetHttpReqMaxBodySize() {
    return s_http_request_max_body_size;
}

uint64_t HttpRspParser::GetHttpRspBufferSize() {
    return s_http_response_buffer_size;
}

uint64_t HttpRspParser::GetHttpRspMaxBodySize() {
    return s_http_response_max_body_size;
}


namespace {
struct _ReqSizeIniter {
    _ReqSizeIniter() {
        s_http_request_buffer_size = g_http_request_buffer_size->getValue();
        s_http_request_max_body_size = g_http_request_max_body_size->getValue();
        s_http_response_buffer_size = g_http_response_buffer_size->getValue();
        s_http_response_max_body_size = g_http_response_max_body_size->getValue();

        g_http_request_buffer_size->addListener(
                [](const uint64_t& ov, const uint64_t& nv){
                s_http_request_buffer_size = nv;
        });

        g_http_request_max_body_size->addListener(
                [](const uint64_t& ov, const uint64_t& nv){
                s_http_request_max_body_size = nv;
        });

        g_http_response_buffer_size->addListener(
                [](const uint64_t& ov, const uint64_t& nv){
                s_http_response_buffer_size = nv;
        });

        g_http_response_max_body_size->addListener(
                [](const uint64_t& ov, const uint64_t& nv){
                s_http_response_max_body_size = nv;
        });
    }
};
static _ReqSizeIniter _init;
}
//data是http_parser的data指针，at是解析出来的http method字符串，length是字符串的长度
//http_mathod结构体成员  0,  DELETE,      aDELETE
void on_request_method(void *data, const char *at, size_t length) {
    HttpReqParser* parser = static_cast<HttpReqParser*>(data);
    HMethod m = CharsToHMethod(at);

    if(m == HMethod::INVALID_METHOD) {
        YHCHAOS_LOG_WARN(g_logger) << "invalid http request method: "
            << std::string(at, length);
        parser->setError(1000);
        return;
    }
    //设置httpReq的m_method成员
    parser->getData()->setMethod(m);
}

void on_request_uri(void *data, const char *at, size_t length) {
}

void on_request_fragment(void *data, const char *at, size_t length) {
    //YHCHAOS_LOG_INFO(g_logger) << "on_request_fragment:" << std::string(at, length);
    HttpReqParser* parser = static_cast<HttpReqParser*>(data);
    parser->getData()->setFragment(std::string(at, length));
}

void on_request_path(void *data, const char *at, size_t length) {
    HttpReqParser* parser = static_cast<HttpReqParser*>(data);
    parser->getData()->setPath(std::string(at, length));
}

void on_request_query(void *data, const char *at, size_t length) {
    HttpReqParser* parser = static_cast<HttpReqParser*>(data);
    parser->getData()->setQuery(std::string(at, length));
}

void on_request_version(void *data, const char *at, size_t length) {
    HttpReqParser* parser = static_cast<HttpReqParser*>(data);
    uint8_t v = 0;
    if(strncmp(at, "HTTP/1.1", length) == 0) {
        v = 0x11;
    } else if(strncmp(at, "HTTP/1.0", length) == 0) {
        v = 0x10;
    } else {
        YHCHAOS_LOG_WARN(g_logger) << "invalid http request version: "
            << std::string(at, length);
        parser->setError(1001);
        return;
    }
    parser->getData()->setVersion(v);
}

void on_request_header_done(void *data, const char *at, size_t length) {
    //HttpReqParser* parser = static_cast<HttpReqParser*>(data);
}

void on_request_http_field(void *data, const char *field, size_t flen
                           ,const char *value, size_t vlen) {
    HttpReqParser* parser = static_cast<HttpReqParser*>(data);
    if(flen == 0) {
        YHCHAOS_LOG_WARN(g_logger) << "invalid http request field length == 0";
        //parser->setError(1002);
        return;
    }
    parser->getData()->setHeader(std::string(field, flen)
                                ,std::string(value, vlen));
}

HttpReqParser::HttpReqParser()
    :m_error(0) {
    m_data.reset(new yhchaos::http::HttpReq);
    http_parser_init(&m_parser);
    //typedef void (*element_cb)(void *data, const char *at, size_t length);
    m_parser.request_method = on_request_method;
    m_parser.request_uri = on_request_uri;
    m_parser.fragment = on_request_fragment;
    m_parser.request_path = on_request_path;
    m_parser.query_string = on_request_query;
    m_parser.http_version = on_request_version;
    m_parser.header_done = on_request_header_done;
    //typedef void (*field_cb)(void *data, const char *field, size_t flen, const char *value, size_t vlen);
    m_parser.http_field = on_request_http_field;
    m_parser.data = this;
}

uint64_t HttpReqParser::getContentLength() {
    return m_data->getHeaderAs<uint64_t>("content-length", 0);
}

//1: 成功
//-1: 有错误
//>0: 已经解析的http原文字节数，且data有效数据为len - v;
size_t HttpReqParser::execute(char* data, size_t len) {
    //len是
    size_t offset = http_parser_execute(&m_parser, data, len, 0);
    memmove(data, data + offset, (len - offset));
    return offset;
}

int HttpReqParser::isFinished() {
    return http_parser_finish(&m_parser);
}

int HttpReqParser::hasError() {
    return m_error || http_parser_has_error(&m_parser);
}

void on_response_reason(void *data, const char *at, size_t length) {
    HttpRspParser* parser = static_cast<HttpRspParser*>(data);
    parser->getData()->setReason(std::string(at, length));
}

void on_response_status(void *data, const char *at, size_t length) {
    HttpRspParser* parser = static_cast<HttpRspParser*>(data);
    HStatus status = (HStatus)(atoi(at));
    parser->getData()->setStatus(status);
}

void on_response_chunk(void *data, const char *at, size_t length) {
}

void on_response_version(void *data, const char *at, size_t length) {
    HttpRspParser* parser = static_cast<HttpRspParser*>(data);
    uint8_t v = 0;
    if(strncmp(at, "HTTP/1.1", length) == 0) {
        v = 0x11;
    } else if(strncmp(at, "HTTP/1.0", length) == 0) {
        v = 0x10;
    } else {
        YHCHAOS_LOG_WARN(g_logger) << "invalid http response version: "
            << std::string(at, length);
        parser->setError(1001);
        return;
    }

    parser->getData()->setVersion(v);
}

void on_response_header_done(void *data, const char *at, size_t length) {
}

void on_response_last_chunk(void *data, const char *at, size_t length) {
}

void on_response_http_field(void *data, const char *field, size_t flen
                           ,const char *value, size_t vlen) {
    HttpRspParser* parser = static_cast<HttpRspParser*>(data);
    if(flen == 0) {
        YHCHAOS_LOG_WARN(g_logger) << "invalid http response field length == 0";
        //parser->setError(1002);
        return;
    }
    parser->getData()->setHeader(std::string(field, flen)
                                ,std::string(value, vlen));
}

HttpRspParser::HttpRspParser()
    :m_error(0) {
    m_data.reset(new yhchaos::http::HttpRsp);
    httpclient_parser_init(&m_parser);
    m_parser.reason_phrase = on_response_reason;
    m_parser.status_code = on_response_status;
    m_parser.chunk_size = on_response_chunk;
    m_parser.http_version = on_response_version;
    m_parser.header_done = on_response_header_done;
    m_parser.last_chunk = on_response_last_chunk;
    m_parser.http_field = on_response_http_field;
    m_parser.data = this;
}

size_t HttpRspParser::execute(char* data, size_t len, bool chunck) {
    if(chunck) {
        httpclient_parser_init(&m_parser);
    }
    size_t offset = httpclient_parser_execute(&m_parser, data, len, 0);

    memmove(data, data + offset, (len - offset));
    return offset;
}

int HttpRspParser::isFinished() {
    return httpclient_parser_finish(&m_parser);
}

int HttpRspParser::hasError() {
    return m_error || httpclient_parser_has_error(&m_parser);
}

uint64_t HttpRspParser::getContentLength() {
    return m_data->getHeaderAs<uint64_t>("content-length", 0);
}

}
}
