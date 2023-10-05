#include "yhchaos/appconfig.h"
#include "yhchaos/environment.h"
#include "yhchaos/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

AppConfigVarBase::ptr AppConfig::SearchForBase(const std::string& name) {
    RWMtxType::ReadLock lock(GetMtx());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str
//先把prefix：node添加到list中
//如果node是一个map，那么prefix.键作为list的first，值作为list的second
//如果值也是一个map，那么就将“prefix.键.值的键”作为list的first，“值的值”作为list的second，递归的遍历完整个map


static void ListAllMember(const std::string& prefix,
                          const YAML::Node& node,
                          std::list<std::pair<std::string, const YAML::Node> >& output) {
    if(prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
            != std::string::npos) {
        YHCHAOS_LOG_ERROR(g_logger) << "AppConfig invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if(node.IsMap()) {
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar()
                    : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

void AppConfig::ResolveFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node> > all_nodes;
    ListAllMember("", root, all_nodes);

    for(auto& i : all_nodes) {
        std::string key = i.first;
        if(key.empty()) {
            continue;
        }

        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        AppConfigVarBase::ptr var = SearchForBase(key);

        if(var) {
            if(i.second.IsScalar()) {
                var->fromString(i.second.Scalar());
            } else {
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }
}

static std::map<std::string, uint64_t> s_file2modifytime;
static yhchaos::Mtx s_mutex;

/**
 * 在给定的 C++ 代码中，`force` 参数用于控制是否强制重新加载没有修改过的配置文件。让我们详细解释一下它的作用：

1. 如果 `force` 参数为 `false`：
   - 当加载配置文件之前，函数首先检查文件的最后修改时间（`st_mtime`）与之前加载时记录的最后修改时间是否相同。
   - 如果两者相同，表示文件没有发生修改，那么函数将跳过该文件的加载，不进行重新加载。
   - 这个检查是为了避免不必要的配置文件加载操作，以提高性能。如果配置文件没有发生变化，就不需要重新加载它。

2. 如果 `force` 参数为 `true`：
   - 不论配置文件的最后修改时间是否改变，都会强制重新加载配置文件。
   - 即使文件没有修改，也会执行重新加载操作。

`force` 参数的存在是为了在需要时手动强制重新加载配置文件，以确保在特定情况下能够立即获取最新的配置。如果不需要强制重新加载，可以将 `force` 参数设置为 `false`，以便在文件未发生修改时跳过加载操作，从而节省不必要的资源和时间。

总之，`force` 参数允许在需要时控制是否强制重新加载配置文件，以适应不同的应用场景和需求。
*/
void AppConfig::ResolveFromConfDir(const std::string& path, bool force) {
    std::string absoulte_path = yhchaos::EnvironmentMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    //将该文件夹中所有以.yaml结尾和子文件夹中所有以.yml结尾的文件路径添加到files数组中
    FSUtil::ListAllFile(files, absoulte_path, ".yml");

    for(auto& i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st);
            yhchaos::Mtx::Lock lock(s_mutex);
            
            if(!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            s_file2modifytime[i] = st.st_mtime;
        }
        try {
            YAML::Node root = YAML::ResolveFile(i);
            ResolveFromYaml(root);
            YHCHAOS_LOG_INFO(g_logger) << "ResolveConfFile file="
                << i << " ok";
        } catch (...) {
            YHCHAOS_LOG_ERROR(g_logger) << "ResolveConfFile file="
                << i << " failed";
        }
    }
}

void AppConfig::Visit(std::function<void(AppConfigVarBase::ptr)> cb) {
    RWMtxType::ReadLock lock(GetMtx());
    AppConfigVarMap& m = GetDatas();
    for(auto it = m.begin();
            it != m.end(); ++it) {
        cb(it->second);
    }

}

}
