#ifndef __YHCHAOS_DP_DP_MESSAGE_H__
#define __YHCHAOS_DP_DP_MESSAGE_H__

#include "yhchaos/dpprotocol.h"
#include "google/protobuf/message.h"

namespace yhchaos {

class DPBody {
public:
    typedef std::shared_ptr<DPBody> ptr;
    virtual ~DPBody(){}

    void setBody(const std::string& v) { m_body = v;}
    const std::string& getBody() const { return m_body;}
    //从m_body序列化到bytearray中
    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray);
    //从bytearray中反序列化到m_body中
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray);
    /**
     * google/protobuf/message.h 是 Google Protocol Buffers（protobuf）库的头文件之一，
     * 它定义了 google::protobuf::MSG 类（这里T的类型），这是所有 Protocol Buffers 消息类型的基类。
     * 在这个基类以及它的子类中，通常都包括了 ParseFromString 方法，用于将二进制数据解析为 
     * Protocol Buffers 消息对象。
    */
    template<class T>
    std::shared_ptr<T> getAsPB() const {
        try {
            std::shared_ptr<T> data(new T);
            if(data->ParseFromString(m_body)) {
                return data;
            }
        } catch (...) {
        }
        return nullptr;
    }
    //v是google/protobuf/message.h中的MSG类或其子类
    template<class T>
    bool setAsPB(const T& v) {
        try {
            return v.SerializeToString(&m_body);
        } catch (...) {
        }
        return false;
    }
protected:
    std::string m_body;
};

class DPRsp;
class DPReq : public Req, public DPBody {
public:
    typedef std::shared_ptr<DPReq> ptr;

    std::shared_ptr<DPRsp> createRsp();

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;

    /** 
     * DPReq的bytearray格式：
     * ba = | MSGType::REQUEST(int32) | Req::m_sn(uint32_t) | Req::m_cmd(uint32_t) | size_t(DPBody::m_body(string)) | DPBody::m_body(string) |
    */
    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) override;
    //从bytearray解析Req的m_sn、m_cmd和DPBody的m_body
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) override;
};

class DPRsp : public Rsp, public DPBody {
public:
    typedef std::shared_ptr<DPRsp> ptr;

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;
    
    /**
     * DPRsp的bytearray格式：
     * ba = | MSGType::RESPONSE(int32) | Rsp::m_sn(uint32_t) | Rsp::m_cmd(uint32_t) | Rsp::m_res(uint32_t) | 
     * size_t(Rsp::m_resStr(string)) | Rsp::m_resStr(string) | size_t(DPBody::m_body(string)) | DPBody::m_body(string) |
    */
    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) override;
    //从bytearray解析Rsp的m_sn、m_cmd、m_res、m_resStr和DPBody的m_body
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) override;
};

class DPNotify : public Notify, public DPBody {
public:
    typedef std::shared_ptr<DPNotify> ptr;

    virtual std::string toString() const override;
    virtual const std::string& getName() const override;
    virtual int32_t getType() const override;

    /**
     * DPNotify的bytearray格式：
     * ba = | MSGType::NOTIFY(int32) | Notify::m_notify | size_t(DPBody::m_body(string)) | DPBody::m_body(string) |
    */
    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) override;
    //从bytearray解析Notify的m_notify和DPBody的m_body
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) override;
};
//dp协议的头部
struct DPMsgHeader {
    DPMsgHeader();
    uint8_t magic[2];
    uint8_t version;
    //压缩格式，0x01表示gzip
    uint8_t flag;
    //MSG类或子类的大小，即DPReq、DPRsp或DPNotify的大小
    int32_t length;
};
/**
 * DPMSG格式：
 * | DPMsgHeader::magic[2](2*uint_8t) | DPMsgHeader::version(uint8_t) | DPMsgHeader::flag(uint8_t) | DPMsgHeader::length(int32_t) |
 * ZlibStream::encode(byteArray(MSG)) |
 * 
*/
class DPMSGDecoder : public MSGDecoder {
public:
    typedef std::shared_ptr<DPMSGDecoder> ptr;

    /**
     * @brief 将stream中的数据解析成msg
     * @param[in] stream 数据流
     * @return 返回解析成功的消息,失败返回nullptr
     * @details 
     *  1. 先从stream中读取DPMsgHeader,
     *  2. 读header后面的DPReq、DPRsp或DPNotify，写到一个ByteBuffer中，移动m_pos
     *  3. 将m_pos设置为0
     *  4. 如果压缩了，ByteBuffer中的数据解压缩到ZlibStream中的iovec中，然后将这个iovec数组写到一个新的ByteBuffer中，m_pos设置为0
     *  5. 从ByteBuffer中读取数据类型MSG::MSGType, 即DPReq、DPRsp或DPNotify对象msg
     *  6. 调用msg->parseFromByteBuffer将上面的ByteBuffer其解析成DPReq、DPRsp或DPNotify对象并返回
    */
    virtual MSG::ptr parseFrom(Stream::ptr stream) override;
    //将msg用ZlibStream压缩后序列化byteArray中，然后添加DPMsgHeader头部，通过stream发送出去
    virtual int32_t serializeTo(Stream::ptr stream, MSG::ptr msg) override;
};

}

#endif
