#include "yhchaos/http/http_parser.h"
#include "yhchaos/log.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

const char test_request_data[] = "POST / HTTP/1.1\r\n"
                                "Host: www.yhchaos.top\r\n"
                                "Content-Length: 10\r\n\r\n"
                                "1234567890";

void test_request() {
    yhchaos::http::HttpReqParser parser;
    std::string tmp = test_request_data;
    size_t s = parser.execute(&tmp[0], tmp.size());
    YHCHAOS_LOG_ERROR(g_logger) << "execute rt=" << s
        << "has_error=" << parser.hasError()
        << " is_finished=" << parser.isFinished()
        << " total=" << tmp.size()
        << " content_length=" << parser.getContentLength();
    tmp.resize(tmp.size() - s);
    YHCHAOS_LOG_INFO(g_logger) << parser.getData()->toString();
    YHCHAOS_LOG_INFO(g_logger) << tmp;
}

const char test_response_data[] = "HTTP/1.1 200 OK\r\n"
        "Date: Tue, 04 Jun 2019 15:43:56 GMT\r\n"
        "Svr: Apache\r\n"
        "Last-Modified: Tue, 12 Jan 2010 13:48:00 GMT\r\n"
        "ETag: \"51-47cf7e6ee8400\"\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 81\r\n"
        "Cache-Control: max-age=86400\r\n"
        "Expires: Wed, 05 Jun 2019 15:43:56 GMT\r\n"
        "Client: Close\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<html>\r\n"
        "<meta http-equiv=\"refresh\" content=\"0;url=http://www.baidu.com/\">\r\n"
        "</html>\r\n";

void test_response() {
    yhchaos::http::HttpRspParser parser;
    std::string tmp = test_response_data;
    size_t s = parser.execute(&tmp[0], tmp.size(), true);
    YHCHAOS_LOG_ERROR(g_logger) << "execute rt=" << s
        << " has_error=" << parser.hasError()
        << " is_finished=" << parser.isFinished()
        << " total=" << tmp.size()
        << " content_length=" << parser.getContentLength()
        << " tmp[s]=" << tmp[s];

    tmp.resize(tmp.size() - s);

    YHCHAOS_LOG_INFO(g_logger) << parser.getData()->toString();
    YHCHAOS_LOG_INFO(g_logger) << tmp;
}

int main(int argc, char** argv) {
    test_request();
    YHCHAOS_LOG_INFO(g_logger) << "--------------";
    test_response();
    return 0;
}
