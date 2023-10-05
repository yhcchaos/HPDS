#include "yhchaos/dpprotocol.h"
#include "yhchaos/util.h"

namespace yhchaos {

ByteBuffer::ptr MSG::toByteBuffer() {
    ByteBuffer::ptr ba(new ByteBuffer);
    if(serializeToByteBuffer(ba)) {
        return ba;
    }
    return nullptr;
}

Req::Req()
    :m_sn(0)
    ,m_cmd(0) {
}

bool Req::serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    bytearray->writeFuint8(getType());
    bytearray->writeUint32(m_sn);
    bytearray->writeUint32(m_cmd);
    return true;
}

bool Req::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    m_sn = bytearray->readUint32();
    m_cmd = bytearray->readUint32();
    return true;
}

Rsp::Rsp()
    :m_sn(0)
    ,m_cmd(0)
    ,m_res(404)
    ,m_resStr("unhandle") {
}

bool Rsp:: serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    bytearray->writeFuint8(getType());
    bytearray->writeUint32(m_sn);
    bytearray->writeUint32(m_cmd);
    bytearray->writeUint32(m_res);
    bytearray->writeStringVint(m_resStr);
    return true;
}

bool Rsp::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    m_sn = bytearray->readUint32();
    m_cmd = bytearray->readUint32();
    m_res = bytearray->readUint32();
    m_resStr = bytearray->readStringVint();
    return true;
}

Notify::Notify()
    :m_notify(0) {
}

bool Notify::serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    bytearray->writeFuint8(getType());
    bytearray->writeUint32(m_notify);
    return true;
}

bool Notify::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    m_notify = bytearray->readUint32();
    return true;
}

}
