#ifndef __YHCHAOS_HTTP_HTTP_CLIENT_H__
#define __YHCHAOS_HTTP_HTTP_CLIENT_H__

#include "yhchaos/streams/sock_stream.h"
#include "http.h"
#include "yhchaos/uridesc.h"
#include "yhchaos/cpp_thread.h"

#include <list>

namespace yhchaos {
namespace http {

/**
 * @brief HTTP响应结果，每次请求的时候返回一个状态，表示成功或者失败以及里面的内容
 */
struct HttpRes {
    /// 智能指针类型定义
    typedef std::shared_ptr<HttpRes> ptr;
    /**
     * @brief 错误码定义
     */
    enum class Error {
        /// 正常
        OK = 0,
        /// 非法URL
        INVALID_URL = 1,
        /// 无法解析HOST
        INVALID_HOST = 2,
        /// 连接失败
        CONNECT_FAIL = 3,
        /// 连接被对端关闭
        SEND_CLOSE_BY_PEER = 4,
        /// 发送请求产生Sock错误
        SEND_SOCKET_ERROR = 5,
        /// 超时
        TIMEOUT = 6,
        /// 创建Sock失败
        CREATE_SOCKET_ERROR = 7,
        /// 从连接池中取连接失败
        POOL_GET_CONNECTION = 8,
        /// 无效的连接
        POOL_INVALID_CONNECTION = 9,
    };

    /**
     * @brief 构造函数
     * @param[in] _res 错误码
     * @param[in] _response HTTP响应结构体
     * @param[in] _error 错误描述
     */
    HttpRes(int _res
               ,HttpRsp::ptr _response
               ,const std::string& _error)
        :result(_res)
        ,response(_response)
        ,error(_error) {}

    /// 错误码
    int result;
    /// HTTP响应结构体
    HttpRsp::ptr response;
    /// 错误描述
    std::string error;

    std::string toString() const;
};

class HttpClientPool;
/**
 * @brief HTTP客户端类，主动请求其他httpserver的链接我们用httpconnection来封装
 * postman模拟http
 */
class HttpClient : public SockStream {
friend class HttpClientPool;
public:
    /// HTTP客户端类智能指针
    typedef std::shared_ptr<HttpClient> ptr;

    /**
     * @brief 发送HTTP的GET请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoGet(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP的GET请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoGet(UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoPost(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoPost(UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri 请求的url，我们需要额外的解析url，到最好还是会获得一个uri对象，我们是基于uri对象来发送请求的
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoReq(HMethod method
                            , const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoReq(HMethod method
                            , UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] req 请求结构体，转发
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @return 返回HTTP结果结构体
     */
    static HttpRes::ptr DoReq(HttpReq::ptr req
                            , UriDesc::ptr uri
                            , uint64_t timeout_ms);

    /**
     * @brief 构造函数
     * @param[in] sock Sock类
     * @param[in] owner 是否掌握所有权
     */
    HttpClient(Sock::ptr sock, bool owner = true);

    /**
     * @brief 析构函数
     */
    ~HttpClient();

    /**
     * @brief 接收HTTP响应
     */
    HttpRsp::ptr recvRsp();

    /**
     * @brief 发送HTTP请求,需要创建一个httpReq对象
     * @param[in] req HTTP请求结构
     */
    int sendReq(HttpReq::ptr req);

private:
    uint64_t m_createTime = 0;
    //当shared_ptr变为0请求释放资源的次数，即使用了的次数
    uint64_t m_request = 0;
};

class HttpClientPool {
public:
    typedef std::shared_ptr<HttpClientPool> ptr;
    typedef Mtx MtxType;
    //通过uri来设置m_host和m_port
    static HttpClientPool::ptr Create(const std::string& uri
                                   ,const std::string& vhost
                                   ,uint32_t max_size
                                   ,uint32_t max_alive_time
                                   ,uint32_t max_request);

    HttpClientPool(const std::string& host
                       ,const std::string& vhost
                       ,uint32_t port
                       ,bool is_https
                       ,uint32_t max_size
                       ,uint32_t max_alive_time
                       ,uint32_t max_request);
    //从m_conn获取一个已经连接的httpconnection对象，删除第一个连接对象前未连接的对象，如果没有
    //连接对象，就创建一个新的连接对象返回（新建一个socket）
    HttpClient::ptr getClient();


    /**
     * @brief 发送HTTP的GET请求
     * @param[in] url 请求的url,包括path,query,fragment
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doGet(const std::string& url
                          , uint64_t timeout_ms
                          , const std::map<std::string, std::string>& headers = {}
                          , const std::string& body = "");

    /**
     * @brief 发送HTTP的GET请求，获取uri的url，然后调用url版本的doGet
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doGet(UriDesc::ptr uri
                           , uint64_t timeout_ms
                           , const std::map<std::string, std::string>& headers = {}
                           , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doPost(const std::string& url
                           , uint64_t timeout_ms
                           , const std::map<std::string, std::string>& headers = {}
                           , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doPost(UriDesc::ptr uri
                           , uint64_t timeout_ms
                           , const std::map<std::string, std::string>& headers = {}
                           , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doReq(HMethod method
                            , const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doReq(HMethod method
                            , UriDesc::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] req 请求结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @return 返回HTTP结果结构体
     */
    HttpRes::ptr doReq(HttpReq::ptr req
                            , uint64_t timeout_ms);
private:
    static void ReleasePtr(HttpClient* ptr, HttpClientPool* pool);
private:
    std::string m_host;//host
    std::string m_vhost;//vhost
    uint32_t m_port;//m_port(port ? port : (is_https ? 443 : 80))
    uint32_t m_maxSize;//max_size
    uint32_t m_maxAliveTime;//max_alive_time
    uint32_t m_maxReq;//max_request,某个连接使用的最大次数
    bool m_isHttps;//is_https
    //保护m_conns
    MtxType m_mutex;
    //要建立的连接
    std::list<HttpClient*> m_conns;
    std::atomic<int32_t> m_total = {0};
};

}
}

#endif
