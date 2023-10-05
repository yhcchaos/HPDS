#ifndef __YHCHAOS_ENVIRONMENT_H__
#define __YHCHAOS_ENVIRONMENT_H__

#include "yhchaos/singleton.h"
#include "yhchaos/cpp_thread.h"
#include <map>
#include <vector>

namespace yhchaos {

class Environment {
public:
    typedef RWMtx RWMtxType;
    /**
     * @details 
     *  获取当前进程的工作目录，可执行文件路径，参数key:value的数组
    */
    bool init(int argc, char** argv);

    void add(const std::string& key, const std::string& val);
    bool has(const std::string& key);
    void del(const std::string& key);
    std::string get(const std::string& key, const std::string& default_value = "");

    void addHelp(const std::string& key, const std::string& desc);
    void removeHelp(const std::string& key);
    void printHelp();
    //获取当前进程可执行文件路径
    const std::string& getExe() const { return m_exe;}
    //获取当前进程工作目录
    const std::string& getCwd() const { return m_cwd;}

    bool setEnvironment(const std::string& key, const std::string& val);
    std::string getEnvironment(const std::string& key, const std::string& default_value = "");
    std::string getAbsolutePath(const std::string& path) const;
    std::string getAbsoluteWorkPath(const std::string& path) const;
    std::string getAppConfigPath();
private:
    RWMtxType m_mutex;
    std::map<std::string, std::string> m_args;
    std::vector<std::pair<std::string, std::string> > m_helps;

    std::string m_program;
    std::string m_exe;
    std::string m_cwd;
};

typedef yhchaos::Singleton<Environment> EnvironmentMgr;

}

#endif
