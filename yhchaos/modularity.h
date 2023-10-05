#ifndef __YHCHAOS_MODULARITY_H__
#define __YHCHAOS_MODULARITY_H__

#include "yhchaos/stream.h"
#include "yhchaos/singleton.h"
#include "yhchaos/mtx.h"
#include "yhchaos/dp/dp_stream.h"
#include <map>
#include <unordered_map>

namespace yhchaos {
/**
 * extern "C" {
 * Modularity* CreateModularity() {
 *  return XX;
 * }
 * void DestoryModularity(Modularity* ptr) {
 *  delete ptr;
 * }
 * }
 */
class Modularity {
public:
    enum Type {
        MODULE = 0,
        DP = 1,
    };
    typedef std::shared_ptr<Modularity> ptr;
    Modularity(const std::string& name
            ,const std::string& version
            ,const std::string& filename
            ,uint32_t type = MODULE);
    virtual ~Modularity() {}

    virtual void onBeforeArgsParse(int argc, char** argv);
    virtual void onAfterArgsParse(int argc, char** argv);

    virtual bool onResolve();
    virtual bool onUnload();

    virtual bool onConnect(yhchaos::Stream::ptr stream);
    virtual bool onDisconnect(yhchaos::Stream::ptr stream);
    
    virtual bool onSvrReady();
    virtual bool onSvrUp();
    //当收到请求时该如何处理
    virtual bool handleReq(yhchaos::MSG::ptr req
                               ,yhchaos::MSG::ptr rsp
                               ,yhchaos::Stream::ptr stream);
    virtual bool handleNotify(yhchaos::MSG::ptr notify
                              ,yhchaos::Stream::ptr stream);

    virtual std::string statusString();

    const std::string& getName() const { return m_name;}
    const std::string& getVersion() const { return m_version;}
    const std::string& getFilename() const { return m_filename;}
    const std::string& getId() const { return m_id;}

    void setFilename(const std::string& v) { m_filename = v;}

    uint32_t getType() const { return m_type;}

    void registerService(const std::string& server_type,
            const std::string& domain, const std::string& service);
protected:
    std::string m_name;//name
    std::string m_version;//version
    std::string m_filename;//filename
    std::string m_id;//name/version
    uint32_t m_type;//Type::MODULE
};

class DPModularity : public Modularity {
public:
    typedef std::shared_ptr<DPModularity> ptr;
    DPModularity(const std::string& name
               ,const std::string& version
               ,const std::string& filename);

    virtual bool handleDPReq(yhchaos::DPReq::ptr request
                        ,yhchaos::DPRsp::ptr response
                        ,yhchaos::DPStream::ptr stream) = 0;
    virtual bool handleDPNotify(yhchaos::DPNotify::ptr notify
                        ,yhchaos::DPStream::ptr stream) = 0;

    virtual bool handleReq(yhchaos::MSG::ptr req
                               ,yhchaos::MSG::ptr rsp
                               ,yhchaos::Stream::ptr stream);
    virtual bool handleNotify(yhchaos::MSG::ptr notify
                              ,yhchaos::Stream::ptr stream);

};

class ModularityManager {
public:
    typedef RWMtx RWMtxType;

    ModularityManager();

    void add(Modularity::ptr m);
    //删除完之后执行module->onUnload();
    void del(const std::string& name);
    //执行n此module->onUnload();
    void delAll();
    //先获取所有的module files路径，然后再根据module file生成Modularity对象，添加到m_modules和m_type2Modularitys中
    void init();

    Modularity::ptr get(const std::string& name);
    //执行所有module->onConnect(stream);
    void onConnect(Stream::ptr stream);
    //执行所有module->onDisConnect(stream);
    void onDisconnect(Stream::ptr stream);

    void listAll(std::vector<Modularity::ptr>& ms);
    void listByType(uint32_t type, std::vector<Modularity::ptr>& ms);
    //将type的所有module作为cb的参数分别执行
    void foreach(uint32_t type, std::function<void(Modularity::ptr)> cb);
private:
    void initModularity(const std::string& path);
private:
    RWMtxType m_mutex;
    //[m_id->module]
    std::unordered_map<std::string, Modularity::ptr> m_modules;
    //m_type->[m_id->module]
    std::unordered_map<uint32_t
        ,std::unordered_map<std::string, Modularity::ptr> > m_type2Modularitys;
};

typedef yhchaos::Singleton<ModularityManager> ModularityMgr;

}

#endif
