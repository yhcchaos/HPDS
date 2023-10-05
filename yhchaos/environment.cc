#include "environment.h"
#include "yhchaos/log.h"
#include <string.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include "appconfig.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

bool Environment::init(int argc, char** argv) {
    char link[1024] = {0};
    char path[1024] = {0};
    sprintf(link, "/proc/%d/exe", getpid());
    readlink(link, path, sizeof(path));
    // /path/xxx/exe
    m_exe = path;

    auto pos = m_exe.find_last_of("/");
    m_cwd = m_exe.substr(0, pos) + "/";

    m_program = argv[0];
    // -config /path/to/config -file xxxx -d
    const char* now_key = nullptr;
    for(int i = 1; i < argc; ++i) {
        if(argv[i][0] == '-') {
            if(strlen(argv[i]) > 1) {
                if(now_key) {
                    add(now_key, "");
                }
                now_key = argv[i] + 1;
            } else {
                YHCHAOS_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                    << " val=" << argv[i];
                return false;
            }
        } else {
            if(now_key) {
                add(now_key, argv[i]);
                now_key = nullptr;
            } else {
                YHCHAOS_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                    << " val=" << argv[i];
                return false;
            }
        }
    }
    if(now_key) {
        add(now_key, "");
    }
    return true;
}

void Environment::add(const std::string& key, const std::string& val) {
    RWMtxType::WriteLock lock(m_mutex);
    m_args[key] = val;
}

bool Environment::has(const std::string& key) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

void Environment::del(const std::string& key) {
    RWMtxType::WriteLock lock(m_mutex);
    m_args.erase(key);
}

std::string Environment::get(const std::string& key, const std::string& default_value) {
    RWMtxType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_value;
}

void Environment::addHelp(const std::string& key, const std::string& desc) {
    removeHelp(key);
    RWMtxType::WriteLock lock(m_mutex);
    m_helps.push_back(std::make_pair(key, desc));
}

void Environment::removeHelp(const std::string& key) {
    RWMtxType::WriteLock lock(m_mutex);
    for(auto it = m_helps.begin();
            it != m_helps.end();) {
        if(it->first == key) {
            it = m_helps.erase(it);
        } else {
            ++it;
        }
    }
}

void Environment::printHelp() {
    RWMtxType::ReadLock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    for(auto& i : m_helps) {
        std::cout << std::setw(5) << "-" << i.first << " : " << i.second << std::endl;
    }
}

bool Environment::setEnvironment(const std::string& key, const std::string& val) {
    return !setenv(key.c_str(), val.c_str(), 1);
}

std::string Environment::getEnvironment(const std::string& key, const std::string& default_value) {
    const char* v = getenv(key.c_str());
    if(v == nullptr) {
        return default_value;
    }
    return v;
}

std::string Environment::getAbsolutePath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    return m_cwd + path;
}

std::string Environment::getAbsoluteWorkPath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    static yhchaos::AppConfigVar<std::string>::ptr g_server_work_path =
        yhchaos::AppConfig::SearchFor<std::string>("server.work_path");
    return g_server_work_path->getValue() + "/" + path;
}

std::string Environment::getAppConfigPath() {
    return getAbsolutePath(get("c", "conf"));
}

}
