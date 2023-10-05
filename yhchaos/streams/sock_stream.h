#ifndef __YHCHAOS_STREAMS_SOCK_STREAM_H__
#define __YHCHAOS_STREAMS_SOCK_STREAM_H__

#include "yhchaos/stream.h"
#include "yhchaos/sock.h"
#include "yhchaos/mtx.h"
#include "yhchaos/iocoscheduler.h"

namespace yhchaos {

/**
 * @brief Sock流，针对每个socket而言的
 */
class SockStream : public Stream {
public:
    typedef std::shared_ptr<SockStream> ptr;

    /**
     * @brief 构造函数
     * @param[in] sock Sock类
     * @param[in] owner 是否完全控制
     */
    SockStream(Sock::ptr sock, bool owner = true);

    /**
     * @brief 析构函数
     * @details 如果m_owner=true,则close，如果不是完全控制就不close，这里的完全控制
     * 指的是这个这个socket只有这一个流，m_owner=false表示一个socket对应多个socketstream类
     */
    ~SockStream();

    /**
     * @brief 读取数据,recv
     * @param[out] buffer 待接收数据的内存
     * @param[in] length 待接收数据的内存长度
     * @return
     *      @retval >0 返回实际接收到的数据长度
     *      @retval =0 socket被远端关闭
     *      @retval <0 socket错误
     */
    virtual int read(void* buffer, size_t length) override;

    /**
     * @brief 读取数据，从ba中用getWriteBuffers获取可写的内存，然后读取数据到这些内存中，最后setPosition
     * @param[out] ba 接收数据的ByteBuffer
     * @param[in] length 待接收数据的内存长度
     * @return
     *      @retval >0 返回实际接收到的数据长度
     *      @retval =0 socket被远端关闭
     *      @retval <0 socket错误
     */
    virtual int read(ByteBuffer::ptr ba, size_t length) override;

    /**
     * @brief 写入数据
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的内存长度
     * @return
     *      @retval >0 返回实际接收到的数据长度
     *      @retval =0 socket被远端关闭
     *      @retval <0 socket错误
     */
    virtual int write(const void* buffer, size_t length) override;

    /**
     * @brief 写入数据，从ba中用getReadBuffers获取可读的内存，然后写入数据到这些内存中，最后setPosition
     * @param[in] ba 待发送数据的ByteBuffer
     * @param[in] length 待发送数据的内存长度
     * @return
     *      @retval >0 返回实际接收到的数据长度
     *      @retval =0 socket被远端关闭
     *      @retval <0 socket错误
     */
    virtual int write(ByteBuffer::ptr ba, size_t length) override;

    /**
     * @brief 关闭socket
     */
    virtual void close() override;

    /**
     * @brief 返回Sock类
     */
    Sock::ptr getSock() const { return m_socket;}

    /**
     * @brief 返回是否连接
     */
    bool isConnected() const;

    NetworkAddress::ptr getRemoteNetworkAddress();
    NetworkAddress::ptr getLocalNetworkAddress();
    std::string getRemoteNetworkAddressString();
    std::string getLocalNetworkAddressString();
protected:
    /// Sock类
    Sock::ptr m_socket;
    /// 是否主控
    bool m_owner;
};

}

#endif
