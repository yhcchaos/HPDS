#ifndef __YHCHAOS_WS_SESSION_H__
#define __YHCHAOS_WS_SESSION_H__

#include "yhchaos/appconfig.h"
#include "yhchaos/http/http_session.h"
#include <stdint.h>

namespace yhchaos {
namespace http {
#pragma pack(1)
struct WFrameHead {
    enum OPCODE {
        /// 数据分片帧
        CONTINUE = 0,
        /// 文本帧
        TEXT_FRAME = 1,
        /// 二进制帧
        BIN_FRAME = 2,
        /// 断开连接
        CLOSE = 8,
        /// PING
        PING = 0x9,
        /// PONG
        PONG = 0xA
    };
    uint32_t opcode: 4;
    bool rsv3: 1;
    bool rsv2: 1;
    bool rsv1: 1;
    bool fin: 1;
    uint32_t payload: 7;
    bool mask: 1;

    std::string toString() const;
};
#pragma pack()

class WFrameMSG {
public:
    typedef std::shared_ptr<WFrameMSG> ptr;
    WFrameMSG(int opcode = 0, const std::string& data = "");

    int getOpcode() const { return m_opcode;}
    void setOpcode(int v) { m_opcode = v;}

    const std::string& getData() const { return m_data;}
    std::string& getData() { return m_data;}
    void setData(const std::string& v) { m_data = v;}
private:
    int m_opcode;
    std::string m_data;
};

class WSession : public HSession {
public:
    typedef std::shared_ptr<WSession> ptr;
    WSession(Sock::ptr sock, bool owner = true);

    /**
     * @brief 客户端发送websocket请求握手消息
     * @return 返回一个HttpReq，如果不是websocket请求则返回nullptr
    */
    HttpReq::ptr handleShake();
    WFrameMSG::ptr recvMSG();
    int32_t sendMSG(WFrameMSG::ptr msg, bool fin = true);
    int32_t sendMSG(const std::string& msg, int32_t opcode = WFrameHead::TEXT_FRAME, bool fin = true);
    int32_t ping();
    int32_t pong();
private:
    bool handleSvrShake();
    bool handleClientShake();
};

extern yhchaos::AppConfigVar<uint32_t>::ptr g_websocket_message_max_size;

/**
 * @brief 服务器端接收websocket请求消息
 * @param[in] stream 流
 * @param[in] client 接收端是否是客户端，如果是服务器端接收消息，则收到的信息是经过掩码处理的，需要解掩码
 * @details 接受一个客户端消息的所有分片帧的数据部分，对其进行解掩码处理，然后组成一个WFrameMSG
*/
WFrameMSG::ptr WRecvMSG(Stream* stream, bool client);

/**
 * @brief 服务器端发送websocket消息
 * @param[in] stream 流
 * @param[in] msg 消息
 * @param[in] client 发送端是否是客户端，如果是服务器端发送消息，则发送的信息不需要进行掩码处理，否则需要进行掩码处理
 * @param[in] fin 是否是消息的最后一个分片
 * @return 发送的实际字节数，不包括发送的长度和掩码，只包括wssocket的头和数据部分，返回时不关闭流
*/
int32_t WSendMSG(Stream* stream, WFrameMSG::ptr msg, bool client, bool fin);
//发送ping信息
int32_t WPing(Stream* stream);
//发送ping信息的回复pong信息
int32_t WPong(Stream* stream);

}
}

#endif
