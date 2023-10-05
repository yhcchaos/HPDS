#include "http_session.h"
#include "http_parser.h"

namespace yhchaos {
namespace http {

HSession::HSession(Sock::ptr sock, bool owner)
    :SockStream(sock, owner) {
}

HttpReq::ptr HSession::recvReq() {
    HttpReqParser::ptr parser(new HttpReqParser);
    uint64_t buff_size = HttpReqParser::GetHttpReqBufferSize();
    //uint64_t buff_size = 100;
    std::shared_ptr<char> buffer(
            new char[buff_size], [](char* ptr){
                delete[] ptr;
            });
    char* data = buffer.get();
    //因为data会将已经解析的字节删除掉，所以offset指的是data中已经读了，但没有解析的字节数量
    int offset = 0;
    do {
        int len = read(data + offset, buff_size - offset);
        if(len <= 0) {
            close();
            return nullptr;
        }
        len += offset;
        size_t nparse = parser->execute(data, len);
        if(parser->hasError()) {
            close();
            return nullptr;
        }
        offset = len - nparse;
        //当当前nparse=0，从socket读出的报文=buff_size，且上一个offset=0
        //说明上一个循环读出来的数据全解析完了，但没有parser->isFinished()，但这次读出来的报文
        //放满了整个缓冲区却一个字节都没有解析，这证明正在解析的这个部分过大，不能解析出一个完整的成分
        //比如m_headers中的值过长，填充满了整个缓冲区，但还是没有结束，就会造成解析结果返回0，这种是错误的报文
        if(offset == (int)buff_size) {
            close();
            return nullptr;
        }
        //解析完报文了
        if(parser->isFinished()) {
            break;
        }
    } while(true);
    //读取报文体
    int64_t length = parser->getContentLength();
    if(length > 0) {
        std::string body;
        body.resize(length);

        int len = 0;
        if(length >= offset) {
            memcpy(&body[0], data, offset);
            len = offset;
        } else {
            memcpy(&body[0], data, length);
            len = length;
        }
        length -= offset;
        if(length > 0) {
            //接受出body剩余没有接收到的数据
            if(readFixSize(&body[len], length) <= 0) {
                close();
                return nullptr;
            }
        }
        parser->getData()->setBody(body);
    }

    parser->getData()->init();
    return parser->getData();
}

int HSession::sendRsp(HttpRsp::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

}
}
