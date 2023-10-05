#include "yhchaos/zk_cli.h"
#include "yhchaos/log.h"
#include "yhchaos/iocoscheduler.h"

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

int g_argc;

void on_watcher(int type, int stat, const std::string& path, yhchaos::ZKCli::ptr client) {
    YHCHAOS_LOG_INFO(g_logger) << " type=" << type
        << " stat=" << stat
        << " path=" << path
        << " client=" << client
        << " coroutine=" << yhchaos::Coroutine::GetThis()
        << " iomanager=" << yhchaos::IOCoScheduler::GetThis();

    if(stat == ZOO_CONNECTED_STATE) {
        if(g_argc == 1) {
            std::vector<std::string> vals;
            Stat stat;
            int rt = client->getChildren("/", vals, true, &stat);
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "[" << yhchaos::Join(vals.begin(), vals.end(), ",") << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "getChildren error " << rt;
            }
        } else {
            std::string new_val;
            new_val.resize(255);
            int rt = client->create("/zkxxx", "", new_val, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL);
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "[" << new_val.c_str() << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "getChildren error " << rt;
            }

//extern ZOOAPI const int ZOO_SEQUENCE;
//extern ZOOAPI const int ZOO_CONTAINER;
            rt = client->create("/zkxxx", "", new_val, &ZOO_OPEN_ACL_UNSAFE, ZOO_SEQUENCE | ZOO_EPHEMERAL);
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "create [" << new_val.c_str() << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "create error " << rt;
            }

            rt = client->get("/hello", new_val, true);
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "get [" << new_val.c_str() << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "get error " << rt;
            }

            rt = client->create("/hello", "", new_val, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL);
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "get [" << new_val.c_str() << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "get error " << rt;
            }

            rt = client->set("/hello", "xxx");
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "set [" << new_val.c_str() << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "set error " << rt;
            }

            rt = client->del("/hello");
            if(rt == ZOK) {
                YHCHAOS_LOG_INFO(g_logger) << "del [" << new_val.c_str() << "]";
            } else {
                YHCHAOS_LOG_INFO(g_logger) << "del error " << rt;
            }

        }
    } else if(stat == ZOO_EXPIRED_SESSION_STATE) {
        client->reconnect();
    }
}

int main(int argc, char** argv) {
    g_argc = argc;
    yhchaos::IOCoScheduler iom(1);
    yhchaos::ZKCli::ptr client(new yhchaos::ZKCli);
    if(g_argc > 1) {
        YHCHAOS_LOG_INFO(g_logger) << client->init("127.0.0.1:21811", 3000, on_watcher);
        //YHCHAOS_LOG_INFO(g_logger) << client->init("127.0.0.1:21811,127.0.0.1:21812,127.0.0.1:21811", 3000, on_watcher);
        iom.addTimedCoroutine(1115000, [client](){client->close();});
    } else {
        YHCHAOS_LOG_INFO(g_logger) << client->init("127.0.0.1:21811,127.0.0.1:21812,127.0.0.1:21811", 3000, on_watcher);
        iom.addTimedCoroutine(5000, [](){}, true);
    }
    iom.stop();
    return 0;
}
