#include <iostream>
#include "yhchaos/http/http_client.h"
#include "yhchaos/log.h"
#include "yhchaos/iocoscheduler.h"
#include "yhchaos/http/http_parser.h"
#include "yhchaos/streams/zlib_stream.h"
#include <fstream>

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void test_pool() {
    yhchaos::http::HttpClientPool::ptr pool(new yhchaos::http::HttpClientPool(
                "www.yhchaos.top", "", 80, false, 10, 1000 * 30, 5));

    yhchaos::IOCoScheduler::GetThis()->addTimedCoroutine(1000, [pool](){
            auto r = pool->doGet("/", 300);
            YHCHAOS_LOG_INFO(g_logger) << r->toString();
    }, true);
}

void run() {
    yhchaos::NetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress("www.yhchaos.top:80");
    if(!addr) {
        YHCHAOS_LOG_INFO(g_logger) << "get addr error";
        return;
    }

    yhchaos::Sock::ptr sock = yhchaos::Sock::CreateTCP(addr);
    bool rt = sock->connect(addr);
    if(!rt) {
        YHCHAOS_LOG_INFO(g_logger) << "connect " << *addr << " failed";
        return;
    }

    yhchaos::http::HttpClient::ptr conn(new yhchaos::http::HttpClient(sock));
    yhchaos::http::HttpReq::ptr req(new yhchaos::http::HttpReq);
    req->setPath("/blog/");
    req->setHeader("host", "www.yhchaos.top");
    YHCHAOS_LOG_INFO(g_logger) << "req:" << std::endl
        << *req;

    conn->sendReq(req);
    auto rsp = conn->recvRsp();

    if(!rsp) {
        YHCHAOS_LOG_INFO(g_logger) << "recv response error";
        return;
    }
    YHCHAOS_LOG_INFO(g_logger) << "rsp:" << std::endl
        << *rsp;

    std::ofstream ofs("rsp.dat");
    ofs << *rsp;

    YHCHAOS_LOG_INFO(g_logger) << "=========================";

    auto r = yhchaos::http::HttpClient::DoGet("http://www.yhchaos.top/blog/", 300);
    YHCHAOS_LOG_INFO(g_logger) << "result=" << r->result
        << " error=" << r->error
        << " rsp=" << (r->response ? r->response->toString() : "");

    YHCHAOS_LOG_INFO(g_logger) << "=========================";
    test_pool();
}

void test_https() {
    auto r = yhchaos::http::HttpClient::DoGet("http://www.baidu.com/", 300, {
                        {"Accept-Encoding", "gzip, deflate, br"},
                        {"Client", "keep-alive"},
                        {"User-Agent", "curl/7.29.0"}
            });
    YHCHAOS_LOG_INFO(g_logger) << "result=" << r->result
        << " error=" << r->error
        << " rsp=" << (r->response ? r->response->toString() : "");

    //yhchaos::http::HttpClientPool::ptr pool(new yhchaos::http::HttpClientPool(
    //            "www.baidu.com", "", 80, false, 10, 1000 * 30, 5));
    auto pool = yhchaos::http::HttpClientPool::Create(
                    "https://www.baidu.com", "", 10, 1000 * 30, 5);
    yhchaos::IOCoScheduler::GetThis()->addTimedCoroutine(1000, [pool](){
            auto r = pool->doGet("/", 3000, {
                        {"Accept-Encoding", "gzip, deflate, br"},
                        {"User-Agent", "curl/7.29.0"}
                    });
            YHCHAOS_LOG_INFO(g_logger) << r->toString();
    }, true);
}

void test_data() {
    yhchaos::NetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAny("www.baidu.com:80");
    auto sock = yhchaos::Sock::CreateTCP(addr);

    sock->connect(addr);
    const char buff[] = "GET / HTTP/1.1\r\n"
                "connection: close\r\n"
                "Accept-Encoding: gzip, deflate, br\r\n"
                "Host: www.baidu.com\r\n\r\n";
    sock->send(buff, sizeof(buff));

    std::string line;
    line.resize(1024);

    std::ofstream ofs("http.dat", std::ios::binary);
    int total = 0;
    int len = 0;
    while((len = sock->recv(&line[0], line.size())) > 0) {
        total += len;
        ofs.write(line.c_str(), len);
    }
    std::cout << "total: " << total << " tellp=" << ofs.tellp() << std::endl;
    ofs.flush();
}

void test_parser() {
    std::ifstream ifs("http.dat", std::ios::binary);
    std::string content;
    std::string line;
    line.resize(1024);

    int total = 0;
    while(!ifs.eof()) {
        ifs.read(&line[0], line.size());
        content.append(&line[0], ifs.gcount());
        total += ifs.gcount();
    }

    std::cout << "length: " << content.size() << " total: " << total << std::endl;
    yhchaos::http::HttpRspParser parser;
    size_t nparse = parser.execute(&content[0], content.size(), false);
    std::cout << "finish: " << parser.isFinished() << std::endl;
    content.resize(content.size() - nparse);
    std::cout << "rsp: " << *parser.getData() << std::endl;

    auto& client_parser = parser.getParser();
    std::string body;
    int cl = 0;
    do {
        size_t nparse = parser.execute(&content[0], content.size(), true);
        std::cout << "content_len: " << client_parser.content_len
                  << " left: " << content.size()
                  << std::endl;
        cl += client_parser.content_len;
        content.resize(content.size() - nparse);
        body.append(content.c_str(), client_parser.content_len);
        content = content.substr(client_parser.content_len + 2);
    } while(!client_parser.chunks_done);

    std::cout << "total: " << body.size() << " content:" << cl << std::endl;

    yhchaos::ZlibStream::ptr stream = yhchaos::ZlibStream::CreateGzip(false);
    stream->write(body.c_str(), body.size());
    stream->flush();

    body = stream->getRes();

    std::ofstream ofs("http.txt");
    ofs << body;
}

int main(int argc, char** argv) {
    yhchaos::IOCoScheduler iom(2);
    //iom.coschedule(run);
    iom.coschedule(test_https);
    return 0;
}
