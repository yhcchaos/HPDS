#ifndef __YHCHAOS_HTTP_CPP_SERVLET_H__
#define __YHCHAOS_HTTP_CPP_SERVLET_H__

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include "http.h"
#include "http_session.h"
#include "yhchaos/cpp_thread.h"
#include "yhchaos/util.h"

namespace yhchaos {
namespace http {

/**
 * @brief CppServlet封装，一个请求进来，会到某个CppServlet中处理，一个server启动的时候会注册很多的CppServlet
*/
class CppServlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<CppServlet> ptr;

    /**
     * @brief 构造函数
     * @param[in] name 名称
     */
    CppServlet(const std::string& name)
        :m_name(name) {}

    /**
     * @brief 析构函数
     */
    virtual ~CppServlet() {}

    /**
     * @brief 处理请求，可以给server添加一个uri的handler，当命中某个uri的时候会找到某个servelet去调用这个函数
     * @param[in] request HTTP请求，从socket收到数据，然后解析成request之后，传给这个函数进行处理
     * @param[in] response HTTP响应
     * @param[in] session HTTP连接
     * @return 是否处理成功
     */
    virtual int32_t handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) = 0;
                   
    /**
     * @brief 返回CppServlet名称
     */
    const std::string& getName() const { return m_name;}
protected:
    /// 名称
    std::string m_name;
};

/**
 * @brief 函数式CppServlet
 */
class FunctionCppServlet : public CppServlet {
public:
    typedef std::shared_ptr<FunctionCppServlet> ptr;
    typedef std::function<int32_t (yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session)> callback;


    /**
     * @brief 构造函数
     * @param[in] cb 回调函数
     */
    FunctionCppServlet(callback cb);
    virtual int32_t handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) override;
private:
    /// 回调函数，可以通过function的方式，而非一定要继承的方式才能支持servlet
    callback m_cb;
};

class ICppServletCreator {
public:
    typedef std::shared_ptr<ICppServletCreator> ptr;
    virtual ~ICppServletCreator() {}
    virtual CppServlet::ptr get() const = 0;
    virtual std::string getName() const = 0;
};


class HoldCppServletCreator : public ICppServletCreator {
public:
    typedef std::shared_ptr<HoldCppServletCreator> ptr;
    HoldCppServletCreator(CppServlet::ptr slt)
        :m_servlet(slt) {
    }

    CppServlet::ptr get() const override {
        return m_servlet;
    }

    std::string getName() const override {
        return m_servlet->getName();
    }
private:
    CppServlet::ptr m_servlet;
};

template<class T>
class CppServletCreator : public ICppServletCreator {
public:
    typedef std::shared_ptr<CppServletCreator> ptr;

    CppServletCreator() {
    }
    //以默认构造函数的方式创建一个servlet，他不能创建functionCppServlet，因为functionCppServlet需要传入一个回调函数
    CppServlet::ptr get() const override {
        return CppServlet::ptr(new T);
    }

    std::string getName() const override {
        return TypeToName<T>();
    }
};

/**
 * @brief CppServlet分发器，是一个特殊的servlet，由其handle函数决定具体去请求哪一个servlet
 */
class CppServletDispatch : public CppServlet {
public:
    typedef std::shared_ptr<CppServletDispatch> ptr;
    typedef RWMtx RWMtxType;

    /**
     * @brief 构造函数
     */
    CppServletDispatch();
    virtual int32_t handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) override;

    /**
     * @brief 添加servlet
     * @param[in] uri uri
     * @param[in] slt serlvet
     */
    void addCppServlet(const std::string& uri, CppServlet::ptr slt);

    /**
     * @brief 添加servlet
     * @param[in] uri uri
     * @param[in] cb FunctionCppServlet回调函数
     */
    void addCppServlet(const std::string& uri, FunctionCppServlet::callback cb);

    /**
     * @brief 添加模糊匹配servlet，如果uri已存在，则会覆盖
     * @param[in] uri uri 模糊匹配 /yhchaos_*
     * @param[in] slt servlet
     */
    void addGlobCppServlet(const std::string& uri, CppServlet::ptr slt);

    /**
     * @brief 添加模糊匹配servlet，如果uri已存在，则会覆盖
     * @param[in] uri uri 模糊匹配 /yhchaos_*
     * @param[in] cb FunctionCppServlet回调函数
     */
    void addGlobCppServlet(const std::string& uri, FunctionCppServlet::callback cb);
    void addCppServletCreator(const std::string& uri, ICppServletCreator::ptr creator);
    void addGlobCppServletCreator(const std::string& uri, ICppServletCreator::ptr creator);
    template<class T>
    void addCppServletCreator(const std::string& uri) {
        addCppServletCreator(uri, std::make_shared<CppServletCreator<T> >());
    }
    template<class T>
    void addGlobCppServletCreator(const std::string& uri) {
        addGlobCppServletCreator(uri, std::make_shared<CppServletCreator<T> >());
    }

    /**
     * @brief 删除servlet
     * @param[in] uri uri
     */
    void delCppServlet(const std::string& uri);

    /**
     * @brief 删除模糊匹配servlet
     * @param[in] uri uri
     */
    void delGlobCppServlet(const std::string& uri);

    /**
     * @brief 返回默认servlet
     */
    CppServlet::ptr getDefault() const { return m_default;}

    /**
     * @brief 设置默认servlet
     * @param[in] v servlet
     */
    void setDefault(CppServlet::ptr v) { m_default = v;}


    /**
     * @brief 通过uri获取servlet
     * @param[in] uri uri
     * @return 返回对应的servlet
     */
    CppServlet::ptr getCppServlet(const std::string& uri);

    /**
     * @brief 通过uri获取模糊匹配servlet
     * @param[in] uri uri
     * @return 返回对应的servlet
     */
    CppServlet::ptr getGlobCppServlet(const std::string& uri);

    /**
     * @brief 通过uri获取servlet
     * @param[in] uri uri
     * @return 优先精准匹配,其次模糊匹配,最后返回默认
     */
    CppServlet::ptr getMatchedCppServlet(const std::string& uri);

    void listAllCppServletCreator(std::map<std::string, ICppServletCreator::ptr>& infos);
    void listAllGlobCppServletCreator(std::map<std::string, ICppServletCreator::ptr>& infos);
private:
    RWMtxType m_mutex;
    std::unordered_map<std::string, ICppServletCreator::ptr> m_datas;
    /// 模糊匹配servlet 数组，上面不匹配的时候使用
    std::vector<std::pair<std::string, ICppServletCreator::ptr> > m_globs;
    CppServlet::ptr m_default;
};

/**
 * @brief NotFoundCppServlet(默认返回404)
 */
class NotFoundCppServlet : public CppServlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<NotFoundCppServlet> ptr;
    /**
     * @brief 构造函数
     */
    NotFoundCppServlet(const std::string& name);
    //根据request填充response对象，status=NOT_FOUND
    virtual int32_t handle(yhchaos::http::HttpReq::ptr request
                   , yhchaos::http::HttpRsp::ptr response
                   , yhchaos::http::HSession::ptr session) override;

private:
    std::string m_name;
    std::string m_content;
};

}
}

#endif
