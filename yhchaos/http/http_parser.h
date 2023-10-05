#ifndef __YHCHAOS_HTTP_HTTP_PARSER_H__
#define __YHCHAOS_HTTP_HTTP_PARSER_H__

#include "http.h"
#include "http11_parser.h"
#include "httpclient_parser.h"

namespace yhchaos {
namespace http {

/**
 * @brief HTTP请求解析类
 */
class HttpReqParser {
public:
    /// HTTP解析类的智能指针
    typedef std::shared_ptr<HttpReqParser> ptr;

    /**
     * @brief 构造函数,初始化http_parser,将其中的函数对象设置为HttpReqParser中的成员函数，
     */
    HttpReqParser();

    /**
     * @brief 解析协议，可能一个报文在socket中要接受多次才能接受完整，所以可能用execute多次来解析一个完整的http报文
     * @param[in, out] data 协议文本内存
     * @param[in] len 协议文本内存长度
     * @return 返回实际解析的长度,并且将已解析的数据移除
     */
    size_t execute(char* data, size_t len);

    /**
     * @brief 是否解析完成
     * @return 是否解析完成
     */
    int isFinished();

    /**
     * @brief 是否有错误
     * @return 是否有错误
     */
    int hasError(); 

    /**
     * @brief 返回HttpReq结构体
     */
    HttpReq::ptr getData() const { return m_data;}

    /**
     * @brief 设置错误
     * @param[in] v 错误值
     */
    void setError(int v) { m_error = v;}

    /**
     * @brief 获取消息体长度
     */
    uint64_t getContentLength();

    /**
     * @brief 获取http_parser结构体
     */
    const http_parser& getParser() const { return m_parser;}
public:
    /**
     * @brief 返回HttpReq协议解析的缓存大小，即存储被解析的原始报文的缓冲区最大大小
     */
    static uint64_t GetHttpReqBufferSize();

    /**
     * @brief 返回HttpReq协议的最大消息体大小
     */
    static uint64_t GetHttpReqMaxBodySize();
private:
    /// http_parser
    http_parser m_parser;
    /// HttpReq结构
    HttpReq::ptr m_data;//m_data.reset(new yhchaos::http::HttpReq);
    /// 错误码
    /// 1000: invalid method
    /// 1001: invalid version
    /// 1002: invalid field
    int m_error;//0
};

/**
 * @brief Http响应解析结构体
 */
class HttpRspParser {
public:
    /// 智能指针类型
    typedef std::shared_ptr<HttpRspParser> ptr;

    /**
     * @brief 构造函数
     */
    HttpRspParser();

    /**
     * @brief 解析HTTP响应协议
     * @param[in, out] data 协议数据内存
     * @param[in] len 协议数据内存大小
     * @param[in] chunck 是否在解析chunck
     * @return 返回实际解析的长度,并且移除已解析的数据
     */
    size_t execute(char* data, size_t len, bool chunck);

    /**
     * @brief 是否解析完成
     */
    int isFinished();

    /**
     * @brief 是否有错误
     */
    int hasError(); 

    /**
     * @brief 返回HttpRsp
     */
    HttpRsp::ptr getData() const { return m_data;}

    /**
     * @brief 设置错误码
     * @param[in] v 错误码
     */
    void setError(int v) { m_error = v;}

    /**
     * @brief 获取消息体长度
     */
    uint64_t getContentLength();

    /**
     * @brief 返回httpclient_parser
     */
    const httpclient_parser& getParser() const { return m_parser;}
public:
    /**
     * @brief 返回HTTP响应解析缓存大小
     */
    static uint64_t GetHttpRspBufferSize();

    /**
     * @brief 返回HTTP响应最大消息体大小
     */
    static uint64_t GetHttpRspMaxBodySize();
private:
    /// httpclient_parser
    httpclient_parser m_parser;//m_data.reset(new yhchaos::http::HttpRsp);
    /// HttpRsp
    HttpRsp::ptr m_data;
    /// 错误码
    /// 1001: invalid version
    /// 1002: invalid field
    int m_error;
};

}
}

#endif
