#include "ws_session.h"
#include "yhchaos/log.h"
#include "yhchaos/endian.h"
#include <string.h>

namespace yhchaos {
namespace http {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

yhchaos::AppConfigVar<uint32_t>::ptr g_websocket_message_max_size
    = yhchaos::AppConfig::SearchFor("websocket.message.max_size"
            ,(uint32_t) 1024 * 1024 * 32, "websocket message max size");

WSession::WSession(Sock::ptr sock, bool owner)
    :HSession(sock, owner) {
}

HttpReq::ptr WSession::handleShake() {
    HttpReq::ptr req;
    do {
        req = recvReq(); 
        if(!req) {
            YHCHAOS_LOG_INFO(g_logger) << "invalid http request";
            break;
        }
        if(strcasecmp(req->getHeader("Upgrade").c_str(), "websocket")) {
            YHCHAOS_LOG_INFO(g_logger) << "http header Upgrade != websocket";
            break;
        }
        if(strcasecmp(req->getHeader("Client").c_str(), "Upgrade")) {
            YHCHAOS_LOG_INFO(g_logger) << "http header Client != Upgrade";
            break;
        }
        if(req->getHeaderAs<int>("Sec-webSock-Version") != 13) {
            YHCHAOS_LOG_INFO(g_logger) << "http header Sec-webSock-Version != 13";
            break;
        }
        std::string key = req->getHeader("Sec-WebSock-Key");
        if(key.empty()) {
            YHCHAOS_LOG_INFO(g_logger) << "http header Sec-WebSock-Key = null";
            break;
        }

        std::string v = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        v = yhchaos::base64encode(yhchaos::sha1sum(v));
        req->setWebsocket(true);

        auto rsp = req->createRsp();
        rsp->setStatus(HStatus::SWITCHING_PROTOCOLS);
        rsp->setWebsocket(true);
        rsp->setReason("Web Sock Protocol Handshake");
        rsp->setHeader("Upgrade", "websocket");
        rsp->setHeader("Client", "Upgrade");
        rsp->setHeader("Sec-WebSock-Accept", v);

        sendRsp(rsp);
        YHCHAOS_LOG_DEBUG(g_logger) << *req;
        YHCHAOS_LOG_DEBUG(g_logger) << *rsp;
        return req;
    } while(false);
    if(req) {
        YHCHAOS_LOG_INFO(g_logger) << *req;
    }
    return nullptr;
}

WFrameMSG::WFrameMSG(int opcode, const std::string& data)
    :m_opcode(opcode)
    ,m_data(data) {
}

std::string WFrameHead::toString() const {
    std::stringstream ss;
    ss << "[WFrameHead fin=" << fin
       << " rsv1=" << rsv1
       << " rsv2=" << rsv2
       << " rsv3=" << rsv3
       << " opcode=" << opcode
       << " mask=" << mask
       << " payload=" << payload
       << "]";
    return ss.str();
}

WFrameMSG::ptr WSession::recvMSG() {
    return WRecvMSG(this, false);
}

int32_t WSession::sendMSG(WFrameMSG::ptr msg, bool fin) {
    return WSendMSG(this, msg, false, fin);
}

int32_t WSession::sendMSG(const std::string& msg, int32_t opcode, bool fin) {
    return WSendMSG(this, std::make_shared<WFrameMSG>(opcode, msg), false, fin);
}

int32_t WSession::ping() {
    return WPing(this);
}

WFrameMSG::ptr WRecvMSG(Stream* stream, bool client) {
    int opcode = 0;
    std::string data;
    int cur_len = 0;
    do {
        WFrameHead ws_head;
        if(stream->readFixSize(&ws_head, sizeof(ws_head)) <= 0) {
            break;
        }
        YHCHAOS_LOG_DEBUG(g_logger) << "WFrameHead " << ws_head.toString();

        if(ws_head.opcode == WFrameHead::PING) {
            YHCHAOS_LOG_INFO(g_logger) << "PING";
            if(WPong(stream) <= 0) {
                break;
            }
        } else if(ws_head.opcode == WFrameHead::PONG) {
        } else if(ws_head.opcode == WFrameHead::CONTINUE
                || ws_head.opcode == WFrameHead::TEXT_FRAME
                || ws_head.opcode == WFrameHead::BIN_FRAME) {
            //服务器端的ws_head.mask必须为1,表示有效负载需要进行掩码处理。client=true
            //也就是说客户端发送给服务器端的数据，客户端必须进行掩码处理
            if(!client && !ws_head.mask) {
                YHCHAOS_LOG_INFO(g_logger) << "WFrameHead mask != 1";
                break;
            }
            uint64_t length = 0;
            /**
             * 解析WebSock帧的有效负载长度（payload length）的部分。WebSock头部中的
             * payload length字段可以采用不同的长度编码，根据实际情况可能是7位、16位或64位，因此需要进行不同的处理。
             * 如果 ws_head.payload 等于 126，则表示payload length采用16位编码。
             * 如果 ws_head.payload 等于 127，则表示payload length采用64位编码。
             * 如果 ws_head.payload 不等于 126 或 127，则表示payload length采用7位编码
            */
            if(ws_head.payload == 126) {
                uint16_t len = 0;
                if(stream->readFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
                length = yhchaos::swapbyteOnLittleEndian(len);
            } else if(ws_head.payload == 127) {
                uint64_t len = 0;
                if(stream->readFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
                length = yhchaos::swapbyteOnLittleEndian(len);
            } else {
                length = ws_head.payload;
            }

            if((cur_len + length) >= g_websocket_message_max_size->getValue()) {
                YHCHAOS_LOG_WARN(g_logger) << "WFrameMSG length > "
                    << g_websocket_message_max_size->getValue()
                    << " (" << (cur_len + length) << ")";
                break;
            }

            char mask[4] = {0};
            if(ws_head.mask) {
                if(stream->readFixSize(mask, sizeof(mask)) <= 0) {
                    break;
                }
            }
            data.resize(cur_len + length);
            if(stream->readFixSize(&data[cur_len], length) <= 0) {
                break;
            }
            if(ws_head.mask) {
                for(int i = 0; i < (int)length; ++i) {
                    data[cur_len + i] ^= mask[i % 4];
                }
            }
            cur_len += length;
            //opcode表示第一个分片帧类型，表示后面的分片帧都是这个类型
            if(!opcode && ws_head.opcode != WFrameHead::CONTINUE) {
                opcode = ws_head.opcode;
            }
            //知道遇到fin为止。表示某个消息的分片帧接收完毕
            if(ws_head.fin) {
                YHCHAOS_LOG_DEBUG(g_logger) << data;
                return WFrameMSG::ptr(new WFrameMSG(opcode, std::move(data)));
            }
        } else {
            YHCHAOS_LOG_DEBUG(g_logger) << "invalid opcode=" << ws_head.opcode;
        }
    } while(true);
    stream->close();
    return nullptr;
}

int32_t WSendMSG(Stream* stream, WFrameMSG::ptr msg, bool client, bool fin) {
    do {
        WFrameHead ws_head;
        memset(&ws_head, 0, sizeof(ws_head));
        ws_head.fin = fin;
        ws_head.opcode = msg->getOpcode();
        ws_head.mask = client;
        uint64_t size = msg->getData().size();
        if(size < 126) {
            ws_head.payload = size;
        } else if(size < 65536) {
            ws_head.payload = 126;
        } else {
            ws_head.payload = 127;
        }
        
        if(stream->writeFixSize(&ws_head, sizeof(ws_head)) <= 0) {
            break;
        }
        if(ws_head.payload == 126) {
            uint16_t len = size;
            len = yhchaos::swapbyteOnLittleEndian(len);
            if(stream->writeFixSize(&len, sizeof(len)) <= 0) {
                break;
            }
        } else if(ws_head.payload == 127) {
            uint64_t len = yhchaos::swapbyteOnLittleEndian(size);
            if(stream->writeFixSize(&len, sizeof(len)) <= 0) {
                break;
            }
        }
        if(client) {
            char mask[4];
            uint32_t rand_value = rand();
            memcpy(mask, &rand_value, sizeof(mask));
            std::string& data = msg->getData();
            for(size_t i = 0; i < data.size(); ++i) {
                data[i] ^= mask[i % 4];
            }

            if(stream->writeFixSize(mask, sizeof(mask)) <= 0) {
                break;
            }
        }
        if(stream->writeFixSize(msg->getData().c_str(), size) <= 0) {
            break;
        }
        return size + sizeof(ws_head);
    } while(0);
    stream->close();
    return -1;
}

int32_t WSession::pong() {
    return WPong(this);
}

int32_t WPing(Stream* stream) {
    WFrameHead ws_head;
    memset(&ws_head, 0, sizeof(ws_head));
    ws_head.fin = 1;
    ws_head.opcode = WFrameHead::PING;
    int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
    if(v <= 0) {
        stream->close();
    }
    return v;
}

int32_t WPong(Stream* stream) {
    WFrameHead ws_head;
    memset(&ws_head, 0, sizeof(ws_head));
    ws_head.fin = 1;
    ws_head.opcode = WFrameHead::PONG;
    int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
    if(v <= 0) {
        stream->close();
    }
    return v;
}

}
}
