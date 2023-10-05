#include "dp_message.h"
#include "yhchaos/log.h"
#include "yhchaos/appconfig.h"
#include "yhchaos/endian.h"
#include "yhchaos/streams/zlib_stream.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

static yhchaos::AppConfigVar<uint32_t>::ptr g_dp_protocol_max_length
    = yhchaos::AppConfig::SearchFor("dp.protocol.max_length",
                            (uint32_t)(1024 * 1024 * 64), "dp protocol max length");

static yhchaos::AppConfigVar<uint32_t>::ptr g_dp_protocol_gzip_min_length
    = yhchaos::AppConfig::SearchFor("dp.protocol.gzip_min_length",
                            (uint32_t)(1024 * 4), "dp protocol gizp min length");

bool DPBody::serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    bytearray->writeStringVint(m_body);
    return true;
}

bool DPBody::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    m_body = bytearray->readStringVint();
    return true;
}

std::shared_ptr<DPRsp> DPReq::createRsp() {
    DPRsp::ptr rt(new DPRsp);
    rt->setSn(m_sn);
    rt->setCmd(m_cmd);
    return rt;
}

std::string DPReq::toString() const {
    std::stringstream ss;
    ss << "[DPReq sn=" << m_sn
       << " cmd=" << m_cmd
       << " body.length=" << m_body.size()
       << "]";
    return ss.str();
}

const std::string& DPReq::getName() const {
    static const std::string& s_name = "DPReq";
    return s_name;
}

int32_t DPReq::getType() const {
    return MSG::REQUEST;
}

bool DPReq::serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    try {
        bool v = true;
        v &= Req::serializeToByteBuffer(bytearray);
        v &= DPBody::serializeToByteBuffer(bytearray);
        return v;
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPReq serializeToByteBuffer error";
    }
    return false;
}

bool DPReq::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    try {
        bool v = true;
        v &= Req::parseFromByteBuffer(bytearray);
        v &= DPBody::parseFromByteBuffer(bytearray);
        return v;
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPReq parseFromByteBuffer error";
    }
    return false;
}

std::string DPRsp::toString() const {
    std::stringstream ss;
    ss << "[DPRsp sn=" << m_sn
       << " cmd=" << m_cmd
       << " result=" << m_res
       << " result_msg=" << m_resStr
       << " body.length=" << m_body.size()
       << "]";
    return ss.str();
}

const std::string& DPRsp::getName() const {
    static const std::string& s_name = "DPRsp";
    return s_name;
}

int32_t DPRsp::getType() const {
    return MSG::RESPONSE;
}

bool DPRsp::serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    try {
        bool v = true;
        v &= Rsp::serializeToByteBuffer(bytearray);
        v &= DPBody::serializeToByteBuffer(bytearray);
        return v;
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPRsp serializeToByteBuffer error";
    }
    return false;
}

bool DPRsp::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    try {
        bool v = true;
        v &= Rsp::parseFromByteBuffer(bytearray);
        v &= DPBody::parseFromByteBuffer(bytearray);
        return v;
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPRsp parseFromByteBuffer error";
    }
    return false;
}

std::string DPNotify::toString() const {
    std::stringstream ss;
    ss << "[DPNotify notify=" << m_notify
       << " body.length=" << m_body.size()
       << "]";
    return ss.str();
}

const std::string& DPNotify::getName() const {
    static const std::string& s_name = "DPNotify";
    return s_name;
}

int32_t DPNotify::getType() const {
    return MSG::NOTIFY;
}

bool DPNotify::serializeToByteBuffer(ByteBuffer::ptr bytearray) {
    try {
        bool v = true;
        v &= Notify::serializeToByteBuffer(bytearray);
        v &= DPBody::serializeToByteBuffer(bytearray);
        return v;
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPNotify serializeToByteBuffer error";
    }
    return false;
}

bool DPNotify::parseFromByteBuffer(ByteBuffer::ptr bytearray) {
    try {
        bool v = true;
        v &= Notify::parseFromByteBuffer(bytearray);
        v &= DPBody::parseFromByteBuffer(bytearray);
        return v;
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPNotify parseFromByteBuffer error";
    }
    return false;
}

static const uint8_t s_dp_magic[2] = {0xab, 0xcd};

DPMsgHeader::DPMsgHeader()
    :magic{0xab, 0xcd}
    ,version(1)
    ,flag(0)
    ,length(0) {
}

MSG::ptr DPMSGDecoder::parseFrom(Stream::ptr stream) {
    try {
        DPMsgHeader header;
        //读取DPMsg的头部
        if(stream->readFixSize(&header, sizeof(header)) <= 0) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder decode head error";
            return nullptr;
        }

        if(memcmp(header.magic, s_dp_magic, sizeof(s_dp_magic))) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder head.magic error";
            return nullptr;
        }

        if(header.version != 0x1) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder head.version != 0x1";
            return nullptr;
        }

        header.length = yhchaos::swapbyteOnLittleEndian(header.length);
        if((uint32_t)header.length >= g_dp_protocol_max_length->getValue()) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder head.length("
                                      << header.length << ") >="
                                      << g_dp_protocol_max_length->getValue();
            return nullptr;
        }
        yhchaos::ByteBuffer::ptr ba(new yhchaos::ByteBuffer);
        if(stream->readFixSize(ba, header.length) <= 0) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder read body fail length=" << header.length;
            return nullptr;
        }

        ba->setPosition(0);
        if(header.flag & 0x1) { //gzip
            //解压缩
            auto zstream = yhchaos::ZlibStream::CreateGzip(false);
            if(zstream->write(ba, -1) != Z_OK) {
                YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder ungzip error";
                return nullptr;
            }
            if(zstream->flush() != Z_OK) {
                YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder ungzip flush error";
                return nullptr;
            }
            ba = zstream->getByteBuffer();
        }
        uint8_t type = ba->readFuint8();
        MSG::ptr msg;
        switch(type) {
            case MSG::REQUEST:
                msg.reset(new DPReq);
                break;
            case MSG::RESPONSE:
                msg.reset(new DPRsp);
                break;
            case MSG::NOTIFY:
                msg.reset(new DPNotify);
                break;
            default:
                YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder invalid type=" << (int)type;
                return nullptr;
        }

        if(!msg->parseFromByteBuffer(ba)) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder parseFromByteBuffer fail type=" << (int)type;
            return nullptr;
        }
        return msg;
    } catch (std::exception& e) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder except:" << e.what();
    } catch (...) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder except";
    }
    return nullptr;
}

int32_t DPMSGDecoder::serializeTo(Stream::ptr stream, MSG::ptr msg) {
    DPMsgHeader header;
    auto ba = msg->toByteBuffer();
    ba->setPosition(0);
    
    header.length = ba->getSize();
    if((uint32_t)header.length >= g_dp_protocol_gzip_min_length->getValue()) {
        auto zstream = yhchaos::ZlibStream::CreateGzip(true);
        if(zstream->write(ba, -1) != Z_OK) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder serializeTo gizp error";
            return -1;
        }
        if(zstream->flush() != Z_OK) {
            YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder serializeTo gizp flush error";
            return -2;
        }

        ba = zstream->getByteBuffer();
        header.flag |= 0x1;
        header.length = ba->getSize();
    }
    header.length = yhchaos::swapbyteOnLittleEndian(header.length);
    if(stream->writeFixSize(&header, sizeof(header)) <= 0) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder serializeTo write header fail";
        return -3;
    }
    if(stream->writeFixSize(ba, ba->getReadSize()) <= 0) {
        YHCHAOS_LOG_ERROR(g_logger) << "DPMSGDecoder serializeTo write body fail";
        return -4;
    }
    return sizeof(header) + ba->getSize();
}

}
