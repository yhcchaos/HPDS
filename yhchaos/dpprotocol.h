#ifndef __YHCHAOS_DPPROTOCOL_H__
#define __YHCHAOS_DPPROTOCOL_H__

#include <memory>
#include "yhchaos/stream.h"
#include "yhchaos/bytebuffer.h"

namespace yhchaos {
//发送数据时，要讲数据封装到MSG对象中，相当于给其加了一个头部
class MSG {
public:
    typedef std::shared_ptr<MSG> ptr;
    enum MSGType {
        REQUEST = 1,
        RESPONSE = 2,
        NOTIFY = 3
    };
    virtual ~MSG() {}

    virtual ByteBuffer::ptr toByteBuffer();
    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) = 0;
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) = 0;

    virtual std::string toString() const = 0;
    virtual const std::string& getName() const = 0;
    virtual int32_t getType() const = 0;
};

class MSGDecoder {
public:
    typedef std::shared_ptr<MSGDecoder> ptr;

    virtual ~MSGDecoder() {}
    virtual MSG::ptr parseFrom(Stream::ptr stream) = 0;
    virtual int32_t serializeTo(Stream::ptr stream, MSG::ptr msg) = 0;
};

class Req : public MSG {
public:
    typedef std::shared_ptr<Req> ptr;

    Req();

    uint32_t getSn() const { return m_sn;}
    uint32_t getCmd() const { return m_cmd;}

    void setSn(uint32_t v) { m_sn = v;}
    void setCmd(uint32_t v) { m_cmd = v;}

    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) override;
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) override;
protected:
    uint32_t m_sn;
    uint32_t m_cmd;
};

class Rsp : public MSG {
public:
    typedef std::shared_ptr<Rsp> ptr;

    Rsp();

    uint32_t getSn() const { return m_sn;}
    uint32_t getCmd() const { return m_cmd;}
    uint32_t getRes() const { return m_res;}
    const std::string& getResStr() const { return m_resStr;}

    void setSn(uint32_t v) { m_sn = v;}
    void setCmd(uint32_t v) { m_cmd = v;}
    void setRes(uint32_t v) { m_res = v;}
    void setResStr(const std::string& v) { m_resStr = v;}
    
    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) override;
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) override;
protected:
    uint32_t m_sn;
    uint32_t m_cmd;
    uint32_t m_res;
    std::string m_resStr;
};

class Notify : public MSG {
public:
    typedef std::shared_ptr<Notify> ptr;
    Notify();

    uint32_t getNotify() const { return m_notify;}
    void setNotify(uint32_t v) { m_notify = v;}

    virtual bool serializeToByteBuffer(ByteBuffer::ptr bytearray) override;
    virtual bool parseFromByteBuffer(ByteBuffer::ptr bytearray) override;
protected:
    uint32_t m_notify;
};

}

#endif
