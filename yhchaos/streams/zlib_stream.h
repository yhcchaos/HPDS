#ifndef __YHCHAOS_STREAMS_ZLIB_STREAM_H__
#define __YHCHAOS_STREAMS_ZLIB_STREAM_H__

#include "yhchaos/stream.h"
#include <sys/uio.h>
#include <zlib.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>

namespace yhchaos {

class ZlibStream : public Stream {
public:
    typedef std::shared_ptr<ZlibStream> ptr;

    enum Type {
        ZLIB,
        DEFLATE,
        GZIP
    };

    enum Strategy {
        DEFAULT = Z_DEFAULT_STRATEGY,
        FILTERED = Z_FILTERED,
        HUFFMAN = Z_HUFFMAN_ONLY,
        FIXED = Z_FIXED,
        RLE = Z_RLE
    };

    enum CompressLevel {
        NO_COMPRESSION = Z_NO_COMPRESSION,
        BEST_SPEED = Z_BEST_SPEED,
        BEST_COMPRESSION = Z_BEST_COMPRESSION,
        DEFAULT_COMPRESSION = Z_DEFAULT_COMPRESSION
    };

    static ZlibStream::ptr CreateGzip(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr CreateZlib(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr CreateDeflate(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr Create(bool encode, uint32_t buff_size = 4096,
            Type type = DEFLATE, int level = DEFAULT_COMPRESSION, int window_bits = 15
            ,int memlevel = 8, Strategy strategy = DEFAULT);

    ZlibStream(bool encode, uint32_t buff_size = 4096);
    ~ZlibStream();
    //不能读
    virtual int read(void* buffer, size_t length) override;
    virtual int read(ByteBuffer::ptr ba, size_t length) override;
    /**
     * @brief 写入数据
     * @param[in] buffer 数据指针
     * @param[in] length 数据长度
     * @return 返回是否写入成功
     * @details 如果m_encode=true，则将buffer中的数据压缩到m_buffs中的iovec中;如果m_encode=false，则将buffer中的数据解压缩到m_buffs中的iovec中
    */
    virtual int write(const void* buffer, size_t length) override;
    virtual int write(ByteBuffer::ptr ba, size_t length) override;
    /**
     * @brief 关闭流
     * @details 刷新流
    */
    virtual void close() override;
    /**
     * @brief 刷新流并关闭流
    */
    int flush();

    bool isFree() const { return m_free;}
    void setFree(bool v) { m_free = v;}

    bool isEncode() const { return m_encode;}
    void setEndcode(bool v) { m_encode = v;}

    std::vector<iovec>& getBuffers() { return m_buffs;}
    std::string getRes() const;
    yhchaos::ByteBuffer::ptr getByteBuffer();
private:
    //根据m_encode初始化m_zstream
    int init(Type type = DEFLATE, int level = DEFAULT_COMPRESSION
             ,int window_bits = 15, int memlevel = 8, Strategy strategy = DEFAULT);
    /**
     * @brief 压缩数据
     * @param[in] v 待压缩数据指针数组
     * @param[in] size 待压缩数据指针数组长度
     * @param[in] finish true：写入完后关闭流；false：不刷新，写入数据，最后不关闭流
     * @return 返回是否压缩成功
     * @details 将iovec数组中的数据压缩到m_buffs中的iovec中
    */
    int encode(const iovec* v, const uint64_t& size, bool finish);
     /**
     * @brief 解压缩数据
     * @param[in] v 已压缩数据指针数组
     * @param[in] size 已压缩数据指针数组长度
     * @param[in] finish true：写入完后关闭流；false：不刷新，写入数据，最后不关闭流
     * @return 返回是否解压缩成功
     * @details 将iovec数组中的数据压缩到m_buffs中的iovec中
    */
    int decode(const iovec* v, const uint64_t& size, bool finish);
private:
    z_stream m_zstream;
    uint32_t m_buffSize;
    bool m_encode;
    bool m_free;//true
    std::vector<iovec> m_buffs;
};

}

#endif
