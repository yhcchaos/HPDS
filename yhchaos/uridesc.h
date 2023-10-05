#ifndef __YHCHAOS_URIDESC_H__
#define __YHCHAOS_URIDESC_H__

#include <memory>
#include <string>
#include <stdint.h>
#include "network_address.h"

namespace yhchaos {
/*
uri=[协议名]://[用户名]:[密码]@[主机名]:[端口]/[路径]?[查询参数]#[片段ID]
     foo://username:password@yhchaos.com:8042/over/there?name=ferret#nose
       \_/   \______________/ \_____/  \_/ \_________/ \_________/ \__/
        |           |            |      |       |           |        |
     scheme     authority      host   port     path        query fragment
*/

/**
 * @brief URI类，定义好URI类之后，用有限状态机ragon来解析
 */
class UriDesc {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<UriDesc> ptr;

    /**
     * @brief 创建UriDesc对象
     * @param uri uri字符串
     * @return 解析成功返回UriDesc对象否则返回nullptr
     */
    static UriDesc::ptr Create(const std::string& uri);

    /**
     * @brief 构造函数
     */
    UriDesc();

    /**
     * @brief 返回scheme
     */
    const std::string& getScheme() const { return m_scheme;}

    /**
     * @brief 返回用户信息
     */
    const std::string& getUserinfo() const { return m_userinfo;}

    /**
     * @brief 返回host
     */
    const std::string& getHost() const { return m_host;}

    /**
     * @brief 返回路径
     */
    const std::string& getPath() const;

    /**
     * @brief 返回查询条件
     */
    const std::string& getQuery() const { return m_query;}

    /**
     * @brief 返回fragment
     */
    const std::string& getFragment() const { return m_fragment;}

    /**
     * @brief 返回端口
     */
    int32_t getPort() const;

    /**
     * @brief 设置scheme
     * @param v scheme
     */
    void setScheme(const std::string& v) { m_scheme = v;}

    /**
     * @brief 设置用户信息
     * @param v 用户信息
     */
    void setUserinfo(const std::string& v) { m_userinfo = v;}

    /**
     * @brief 设置host信息
     * @param v host
     */
    void setHost(const std::string& v) { m_host = v;}

    /**
     * @brief 设置路径
     * @param v 路径
     */
    void setPath(const std::string& v) { m_path = v;}

    /**
     * @brief 设置查询条件
     * @param v
     */
    void setQuery(const std::string& v) { m_query = v;}

    /**
     * @brief 设置fragment
     * @param v fragment
     */
    void setFragment(const std::string& v) { m_fragment = v;}

    /**
     * @brief 设置端口号
     * @param v 端口
     */
    void setPort(int32_t v) { m_port = v;}

    /**
     * @brief 序列化到输出流
     * @param os 输出流
     * @return 输出流
     */
    std::ostream& dump(std::ostream& os) const;

    /**
     * @brief 转成字符串
     */
    std::string toString() const;

    /**
     * @brief 获取NetworkAddress，通过uri的域名host获取对应的一个NetworkAddress
     * NetworkAddress::SearchForAnyIPNetworkAddress(m_host)
     */
    NetworkAddress::ptr createNetworkAddress() const;
private:

    /**
     * @brief 是否默认端口
     */
    bool isDefaultPort() const;
private:
    /// schema,协议
    std::string m_scheme;
    /// 用户信息
    std::string m_userinfo;
    /// host
    std::string m_host;
    /// 路径
    std::string m_path;
    /// 查询参数
    std::string m_query;
    /// fragment
    std::string m_fragment;
    /// 端口
    int32_t m_port;//0
};

}

#endif
