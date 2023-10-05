#ifndef __YHCHAOS_SOCK_H__
#define __YHCHAOS_SOCK_H__

#include <memory>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include "network_address.h"
#include "noncopyable.h"

namespace yhchaos {

/**
 * @brief Sock封装类
 */
class Sock : public std::enable_shared_from_this<Sock>, Noncopyable {
public:
    typedef std::shared_ptr<Sock> ptr;
    typedef std::weak_ptr<Sock> weak_ptr;

    /**
     * @brief Sock类型
     */
    enum Type {
        /// TCP类型
        TCP = SOCK_STREAM,
        /// UDP类型
        UDP = SOCK_DGRAM
    };

    /**
     * @brief Sock协议簇
     */
    enum Family {
        /// IPv4 socket
        IPv4 = AF_INET,
        /// IPv6 socket
        IPv6 = AF_INET6,
        /// Unix socket
        UNIX = AF_UNIX,
    };

    /**
     * @brief 创建TCP Sock(满足地址类型)，没创建fd
     * @param[in] address 地址
     */
    static Sock::ptr CreateTCP(yhchaos::NetworkAddress::ptr address);

    /**
     * @brief 创建UDP Sock(满足地址类型)，创建fd，并初始化套接字选项，isConnected = true
     * @param[in] address 地址
     */
    static Sock::ptr CreateUDP(yhchaos::NetworkAddress::ptr address);

    /**
     * @brief 创建IPv4的TCP Sock，没创建fd
     */
    static Sock::ptr CreateTCPSock();

    /**
     * @brief 创建IPv4的UDP Sock，创建fd，并初始化套接字选项，isConnected = true
     */
    static Sock::ptr CreateUDPSock();

    /**
     * @brief 创建IPv6的TCP Sock，没创建fd
     */
    static Sock::ptr CreateTCPSock6();

    /**
     * @brief 创建IPv6的UDP Sock，创建fd，并初始化套接字选项，isConnected = true
     */
    static Sock::ptr CreateUDPSock6();

    /**
     * @brief 创建Unix的TCP Sock，没创建fd
     */
    static Sock::ptr CreateUnixTCPSock();

    /**
     * @brief 创建Unix的UDP Sock，没创建fd
     */
    static Sock::ptr CreateUnixUDPSock();

    /**
     * @brief Sock构造函数，没创建fd
     * @param[in] family 协议簇
     * @param[in] type 类型
     * @param[in] protocol 协议
     */
    Sock(int family, int type, int protocol = 0);

    /**
     * @brief 析构函数，close(m_sock)
     */
    virtual ~Sock();

    /**
     * @brief 获取发送超时时间(毫秒),从fdManager的队列中获取fd的fdCtx，得到fdCtx的sendtimeout
     */
    int64_t getSendTimeout();

    /**
     * @brief 设置发送超时时间(毫秒)，用setsockopt设置套接字的发送超时时间
     */
    void setSendTimeout(int64_t v);

    /**
     * @brief 获取接受超时时间(毫秒)，从fdManager的队列中获取fd的fdCtx，得到fdCtx的recvtimeout
     */
    int64_t getRecvTimeout();

    /**
     * @brief 设置接受超时时间(毫秒)，用setsockopt设置套接字的接受超时时间
     */
    void setRecvTimeout(int64_t v);

    /**
     * @brief 获取sockopt @see getsockopt，获取套接字选项
     */
    bool getOption(int level, int option, void* result, socklen_t* len);

    /**
     * @brief 获取sockopt模板 @see getsockopt，局偶去套接字选项
     */
    template<class T>
    bool getOption(int level, int option, T& result) {
        socklen_t length = sizeof(T);
        return getOption(level, option, &result, &length);
    }

    /**
     * @brief 设置sockopt @see setsockopt，设置套接字选项
     */
    bool setOption(int level, int option, const void* result, socklen_t len);

    /**
     * @brief 设置sockopt模板 @see setsockopt，设置套接字选项
     */
    template<class T>
    bool setOption(int level, int option, const T& value) {
        return setOption(level, option, &value, sizeof(T));
    }

protected:
    /**
     * @brief 初始化socket，设置套接字选项，UDP：SO_REUSEADDR， TCP：SO_REUSEADDR, TCP_NODELAY
     */
    void initSock();

    /**
     * @brief 创建socket，并调用initSock初始化套接字
     */
    void newSock();

    /**
     * @brief 初始化sock，先获取fdManager的队列中的fd的fdCtx，如果fdCtx存在
     * 是socket并且以没有关闭，那么就对调用该方法的Sock对象初始化，从sock获得m_localNetworkAddress和m_remoteNetworkAddress
     * ，设置m_isConnected为true，设置套接字选项，如果上面条件有一项不满足，那么就返回false
     */
    virtual bool init(int sock);
public:
    /**
     * @brief 接收connect连接，调用init初始化新连接，设置套接字选项
     * @return 成功返回新连接的socket,失败返回nullptr
     * @pre Sock必须 bind , listen  成功
     */
    virtual Sock::ptr accept();

    /**
     * @brief 绑定地址,
     * @param[in] addr 地址
     * @return 是否绑定成功
     */
    virtual bool bind(const NetworkAddress::ptr addr);

    /**
     * @brief 连接地址，调用newsock创建socket并设置套接字选项，设置本地和对端地址，设置m_isConnected为true
     * @param[in] addr 目标地址
     * @param[in] timeout_ms 超时时间(毫秒),-1用connect，其他值同hook.c的connect_with_timeout
     */
    virtual bool connect(const NetworkAddress::ptr addr, uint64_t timeout_ms = -1);
    
    //重置本地地址并connect
    virtual bool reconnect(uint64_t timeout_ms = -1);

    /**
     * @brief 监听socket，调用listen
     * @param[in] backlog 未完成连接队列的最大长度
     * @result 返回监听是否成功
     * @pre 必须先 bind 成功
     */
    virtual bool listen(int backlog = SOMAXCONN);

    /**
     * @brief 关闭socket，调用close
     */
    virtual bool close();

    /**
     * @brief 发送数据，调用send
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的长度
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int send(const void* buffer, size_t length, int flags = 0);

    /**
     * @brief 发送数据，调用sendmsg
     * @param[in] buffers 待发送数据的内存(iovec数组)
     * @param[in] length 待发送数据的长度(iovec长度)
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int send(const iovec* buffers, size_t length, int flags = 0);

    /**
     * @brief 发送数据，调用sendto
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的长度
     * @param[in] to 发送的目标地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int sendTo(const void* buffer, size_t length, const NetworkAddress::ptr to, int flags = 0);

    /**
     * @brief 发送数据，调用sendmsg
     * @param[in] buffers 待发送数据的内存(iovec数组)
     * @param[in] length 待发送数据的长度(iovec长度)
     * @param[in] to 发送的目标地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int sendTo(const iovec* buffers, size_t length, const NetworkAddress::ptr to, int flags = 0);

    /**
     * @brief 接受数据，调用recv，
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recv(void* buffer, size_t length, int flags = 0);

    /**
     * @brief 接受数据，调用recvmsg，不需要设置msghdr的对端地址
     * @param[out] buffers 接收数据的内存(iovec数组)
     * @param[in] length 接收数据的内存大小(iovec数组长度)
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recv(iovec* buffers, size_t length, int flags = 0);

    /**
     * @brief 接受数据，设置msghdr的对端地址
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @param[out] from 发送端地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recvFrom(void* buffer, size_t length, NetworkAddress::ptr from, int flags = 0);

    /**
     * @brief 接受数据
     * @param[out] buffers 接收数据的内存(iovec数组)
     * @param[in] length 接收数据的内存大小(iovec数组长度)
     * @param[out] from 发送端地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket被关闭
     *      @retval <0 socket出错
     */
    virtual int recvFrom(iovec* buffers, size_t length, NetworkAddress::ptr from, int flags = 0);

    /**
     * @brief 用getpeername获取远端地址
     */
    NetworkAddress::ptr getRemoteNetworkAddress();

    /**
     * @brief 用getsockname获取本地地址
     */
    NetworkAddress::ptr getLocalNetworkAddress();

    /**
     * @brief 获取协议簇
     */
    int getFamily() const { return m_family;}

    /**
     * @brief 获取类型
     */
    int getType() const { return m_type;}

    /**
     * @brief 获取协议
     */
    int getProtocol() const { return m_protocol;}

    /**
     * @brief 返回是否连接
     */
    bool isConnected() const { return m_isConnected;}

    /**
     * @brief 是否有效(m_sock != -1)
     */
    bool isValid() const;

    /**
     * @brief 返回Sock错误，用getsockopt获取SO_ERROR选项
     */
    int getError();

    /**
     * @brief 输出信息到流中
     */
    virtual std::ostream& dump(std::ostream& os) const;
    //调用dump
    virtual std::string toString() const;

    /**
     * @brief 返回socket句柄
     */
    int getSock() const { return m_sock;}

    /**
     * @brief 取消读，取消当前调度器的读事件cancelFdEvent(m_sock, yhchaos::IOCoScheduler::READ)
     */
    bool cancelRead();

    /**
     * @brief 取消写,取消当前调度器的写事件cancelFdEvent(m_sock, yhchaos::IOCoScheduler::WRITE)
     */
    bool cancelWrite();

    /**
     * @brief 取消accept，取消当前调度器的读事件cancelFdEvent(m_sock, yhchaos::IOCoScheduler::READ)
     */
    bool cancelAccept();

    /**
     * @brief 取消所有事件，取消当前调度器的读写事件cancelAll(fd)
     */
    bool cancelAll();

protected:
    /// socket句柄
    int m_sock;
    /// 协议簇
    int m_family;
    /// 类型
    int m_type;
    /// 协议
    int m_protocol;
    /// 是否连接
    bool m_isConnected;
    /// 本地地址
    NetworkAddress::ptr m_localNetworkAddress;
    /// 远端地址
    NetworkAddress::ptr m_remoteNetworkAddress;
};

class SSLSock : public Sock {
public:
    typedef std::shared_ptr<SSLSock> ptr;

    static SSLSock::ptr CreateTCP(yhchaos::NetworkAddress::ptr address);
    static SSLSock::ptr CreateTCPSock();
    static SSLSock::ptr CreateTCPSock6();

    SSLSock(int family, int type, int protocol = 0);

protected:
    //初始化ssl的相关信息，然后调用SSL_accept，会在accept中调用init
    virtual bool init(int sock) override;

public:
    virtual bool bind(const NetworkAddress::ptr addr) override;
    virtual bool listen(int backlog = SOMAXCONN) override;
    //先调用::accept，然后调用init
    virtual Sock::ptr accept() override;

    //先调用::connect,然后初始化客户端ssl相关信息，然后调用SSL_connect
    virtual bool connect(const NetworkAddress::ptr addr, uint64_t timeout_ms = -1) override;
    virtual bool close() override;

    //SSL_write
    virtual int send(const void* buffer, size_t length, int flags = 0) override;
    virtual int send(const iovec* buffers, size_t length, int flags = 0) override;
    virtual int sendTo(const void* buffer, size_t length, const NetworkAddress::ptr to, int flags = 0) override;
    virtual int sendTo(const iovec* buffers, size_t length, const NetworkAddress::ptr to, int flags = 0) override;
    virtual int recv(void* buffer, size_t length, int flags = 0) override;
    virtual int recv(iovec* buffers, size_t length, int flags = 0) override;
    virtual int recvFrom(void* buffer, size_t length, NetworkAddress::ptr from, int flags = 0) override;
    virtual int recvFrom(iovec* buffers, size_t length, NetworkAddress::ptr from, int flags = 0) override;

    //初始化服务器端的ssl相关信息，包括各种证书
    bool loadCertificates(const std::string& cert_funile, const std::string& key_funile);
    virtual std::ostream& dump(std::ostream& os) const override;

private:
    //用于管理 SSL 上下文对象（SSL_CTX）。它被用于配置 SSL/TLS 参数和选项。
    std::shared_ptr<SSL_CTX> m_ctx;
    //用于管理 SSL 连接对象（SSL）。在初始化后，它会表示一个已建立的 SSL/TLS 连接。
    std::shared_ptr<SSL> m_ssl;
};

/**
 * @brief 流式输出socket
 * @param[in, out] os 输出流
 * @param[in] sock Sock类
 */
std::ostream& operator<<(std::ostream& os, const Sock& sock);

}

#endif
