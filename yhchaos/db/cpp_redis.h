#ifndef __YHCHAOS_DB_CPP_REDIS_H__
#define __YHCHAOS_DB_CPP_REDIS_H__

#include <stdlib.h>
#include <hiredis-vip/hicpp_redis.h>
#include <hiredis-vip/hircluster.h>
#include <hiredis-vip/adapters/libevent.h>
#include <sys/time.h>
#include <string>
#include <memory>
#include "yhchaos/mtx.h"
#include "yhchaos/db/watch_cpp_thread.h"
#include "yhchaos/singleton.h"

namespace yhchaos {

typedef std::shared_ptr<redisReply> ReplyPtr;

class ICppRedis {
public:
    enum Type {
        REDIS = 1,
        REDIS_CLUSTER = 2,
        FOX_REDIS = 3,
        FOX_REDIS_CLUSTER = 4
    };
    typedef std::shared_ptr<ICppRedis> ptr;
    ICppRedis() : m_logEnable(true) { }
    virtual ~ICppRedis() {}

    virtual ReplyPtr cmd(const char* fmt, ...) = 0;
    virtual ReplyPtr cmd(const char* fmt, va_list ap) = 0;
    virtual ReplyPtr cmd(const std::vector<std::string>& argv) = 0;

    const std::string& getName() const { return m_name;}
    void setName(const std::string& v) { m_name = v;}

    const std::string& getPasswd() const { return m_passwd;}
    void setPasswd(const std::string& v) { m_passwd = v;}

    Type getType() const { return m_type;}
protected:
    std::string m_name;
    std::string m_passwd;
    Type m_type;
    bool m_logEnable;//true
};

//同步redis
class ISyncCppRedis : public ICppRedis {
public:
    typedef std::shared_ptr<ISyncCppRedis> ptr;
    virtual ~ISyncCppRedis() {}

    virtual bool reconnect() = 0;
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0) = 0;
    virtual bool connect() = 0;
    virtual bool setTimeout(uint64_t ms) = 0;

    virtual int appendCmd(const char* fmt, ...) = 0;
    virtual int appendCmd(const char* fmt, va_list ap) = 0;
    virtual int appendCmd(const std::vector<std::string>& argv) = 0;

    virtual ReplyPtr getReply() = 0;

    uint64_t getLastActiveTime() const { return m_lastActiveTime;}
    void setLastActiveTime(uint64_t v) { m_lastActiveTime = v;}

protected:
    uint64_t m_lastActiveTime;
};

class CppRedis : public ISyncCppRedis {
public:
    typedef std::shared_ptr<CppRedis> ptr;
    CppRedis();
    /**
     *  redis:
            config:
                local:
                    host: 127.0.0.1:6379
                    type: fox_redis
                    pool: 2
                    timeout: 100
            desc: "type: redis,redis_cluster,fox_redis,fox_redis_cluster"
    */
    CppRedis(const std::map<std::string, std::string>& conf);

    virtual bool reconnect();
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0);
    virtual bool connect();
    virtual bool setTimeout(uint64_t ms);

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    virtual int appendCmd(const char* fmt, ...);
    virtual int appendCmd(const char* fmt, va_list ap);
    virtual int appendCmd(const std::vector<std::string>& argv);

    virtual ReplyPtr getReply();
private:
    std::string m_host;//host:port中的host，单个
    uint32_t m_port;//host:port中的port
    uint32_t m_connectMs;
    struct timeval m_cmdTimeout;//timeout_com ? timeout_com : timeout
    std::shared_ptr<redisContext> m_context;//redis连接上下文
    /**父类成员
     * std::string m_name;
     * std::string m_passwd;
     * Type m_type;     //ICppRedis::REDIS
     * bool m_logEnable;    //true
     * uint64_t m_lastActiveTime;
    */
};

class CppRedisCluster : public ISyncCppRedis {
public:
    typedef std::shared_ptr<CppRedisCluster> ptr;
    CppRedisCluster();
    CppRedisCluster(const std::map<std::string, std::string>& conf);

    virtual bool reconnect();
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0);
    virtual bool connect();
    virtual bool setTimeout(uint64_t ms);

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    virtual int appendCmd(const char* fmt, ...);
    virtual int appendCmd(const char* fmt, va_list ap);
    virtual int appendCmd(const std::vector<std::string>& argv);

    virtual ReplyPtr getReply();
private:
    std::string m_host;
    uint32_t m_port;
    uint32_t m_connectMs;
    struct timeval m_cmdTimeout;
    std::shared_ptr<redisClusterContext> m_context;
    /**父类成员
     * std::string m_name;
     * std::string m_passwd;
     * Type m_type;     ICppRedis::REDIS_CLUSTER;
     * bool m_logEnable;    //true
     * uint64_t m_lastActiveTime;
    */
};

class FoxCppRedis : public ICppRedis {
public:
    typedef std::shared_ptr<FoxCppRedis> ptr;
    enum STATUS {
        UNCONNECTED = 0,
        CONNECTING = 1,
        CONNECTED = 2
    };
    enum RESULT {
        OK = 0,
        TIME_OUT = 1,
        CONNECT_ERR = 2,
        CMD_ERR = 3,
        REPLY_NULL = 4,
        REPLY_ERR = 5,
        INIT_ERR = 6
    };

    FoxCppRedis(yhchaos::WatchCppThread* thr, const std::map<std::string, std::string>& conf);
    ~FoxCppRedis();

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);
    //在m_thread中执行pinit进行初始化连接
    bool init();
    int getCtxCount() const { return m_ctxCount;}

 
private:
    struct FCtx {
        //通过调用redisFormatCommandArgv得到的redis命令协议字符串
        std::string cmd;
        yhchaos::CoScheduler* coscheduler;
        yhchaos::Coroutine::ptr coroutine;
        ReplyPtr rpy;
    };
    //作为发送请求时的pridata参数，收到响应时传递给privdata参数
    struct Ctx {
        typedef std::shared_ptr<Ctx> ptr;

        event* ev;//nullptr
        bool timeout;//false

        FoxCppRedis* rds;//rds
        std::string cmd;
        FCtx* fctx;
        //std::vector<std::string> parts;
        //yhchaos::CoScheduler* coscheduler;
        //yhchaos::Coroutine::ptr coroutine;
        //ReplyPtr rpy;
        WatchCppThread* thread;//nullptr

        //Ctx::ptr ref;

        Ctx(FoxCppRedis* rds);
        ~Ctx();
        //创建一个定时器，如果超时时间内没有收到回复就出发定时器
        bool init();
        void cancelFdEvent();
        static void FdEventCb(int fd, short event, void* d);
    };
private:
    virtual void pcmd(FCtx* ctx);

    /**
     * 1. 异步连接服务器
     * 2. 设置connectCB和disconnectCB
     * 3. 设置定时任务timeCB，定时发送ping命令
     * 4. 立即发送一个ping命令
    */ 
    bool pinit();
    void delayDelete(redisAsyncContext* c);
private:
    //发送身份认证命令的回调函数，发送身份验证信息m_passwd，在连接回调函数中connectCb中连接成功时调用
    static void OnAuthCb(redisAsyncContext* c, void* rp, void* priv);
    //连接成功时的回调函数，发送身份验证信息m_passwd
    static void ConnectCb(const redisAsyncContext* c, int status);
    //断开连接时的回调函数，将状态m_status设置为unconnected
    static void DisconnectCb(const redisAsyncContext* c, int status);
    //执行命令的异步回调函数
    static void CmdCb(redisAsyncContext *c, void *r, void *privdata);
    //定时发送ping命令
    static void TimeCb(int fd, short event, void* d);
private:
    yhchaos::WatchCppThread* m_thread;//thr
    std::shared_ptr<redisAsyncContext> m_context;//0
    std::string m_host;//conf中的host，只有一个服务器
    uint16_t m_port;//conf中port
    STATUS m_status;//UNCONNECTED,连接成功，即ConnectCb回调函数的status为OK时，设置为connected
    int m_ctxCount;//0

    struct timeval m_cmdTimeout;//conf中timeout_com ？ timeout_com ：timeout 
    std::string m_err;
    struct event* m_event;//nullptr
    /**父类成员
    std::string m_name;
    std::string m_passwd;
    Type m_type;
    bool m_logEnable;//true
    */
};

class FoxCppRedisCluster : public ICppRedis {
public:
    typedef std::shared_ptr<FoxCppRedisCluster> ptr;
    enum STATUS {
        UNCONNECTED = 0,
        CONNECTING = 1,
        CONNECTED = 2
    };
    enum RESULT {
        OK = 0,
        TIME_OUT = 1,
        CONNECT_ERR = 2,
        CMD_ERR = 3,
        REPLY_NULL = 4,
        REPLY_ERR = 5,
        INIT_ERR = 6
    };

    FoxCppRedisCluster(yhchaos::WatchCppThread* thr, const std::map<std::string, std::string>& conf);
    ~FoxCppRedisCluster();

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    int getCtxCount() const { return m_ctxCount;}

    bool init();
private:
    struct FCtx {
        std::string cmd;
        yhchaos::CoScheduler* coscheduler;
        yhchaos::Coroutine::ptr coroutine;
        ReplyPtr rpy;
    };
    struct Ctx {
        typedef std::shared_ptr<Ctx> ptr;

        event* ev;
        bool timeout;//1
        FoxCppRedisCluster* rds;
        FCtx* fctx;
        std::string cmd;
        //std::vector<std::string> parts;
        WatchCppThread* thread;//m_thread
        //int cancel_count;
        //int destory;
        //int callback_count;
        //yhchaos::RWMtx mutex;

        //Ctx::ptr ref;
        //Ctx::ptr tref;
        void cancelFdEvent();

        Ctx(FoxCppRedisCluster* rds);
        ~Ctx();
        bool init();
        static void FdEventCb(int fd, short event, void* d);
    };
private:
    virtual void pcmd(FCtx* ctx);
    bool pinit();
    void delayDelete(redisAsyncContext* c);
    static void OnAuthCb(redisClusterAsyncContext* c, void* rp, void* priv);
private:
    static void ConnectCb(const redisAsyncContext* c, int status);
    static void DisconnectCb(const redisAsyncContext* c, int status);
    static void CmdCb(redisClusterAsyncContext*c, void *r, void *privdata);
    static void TimeCb(int fd, short event, void* d);
private:
    yhchaos::WatchCppThread* m_thread;
    std::shared_ptr<redisClusterAsyncContext> m_context;
    std::string m_host;
    STATUS m_status;
    int m_ctxCount;

    struct timeval m_cmdTimeout;
    std::string m_err;
    struct event* m_event;
};

class CppRedisManager {
public:
    CppRedisManager();
    ICppRedis::ptr get(const std::string& name);

    std::ostream& dump(std::ostream& os);
private:
    void freeCppRedis(ICppRedis* r);
    void init();
private:
    yhchaos::RWMtx m_mutex;
    std::map<std::string, std::list<ICppRedis*> > m_datas;
    std::map<std::string, std::map<std::string, std::string> > m_config;
};

typedef yhchaos::Singleton<CppRedisManager> CppRedisMgr;

class CppRedisUtil {
public:
    static ReplyPtr Cmd(const std::string& name, const char* fmt, ...);
    static ReplyPtr Cmd(const std::string& name, const char* fmt, va_list ap); 
    static ReplyPtr Cmd(const std::string& name, const std::vector<std::string>& args); 

    static ReplyPtr TryCmd(const std::string& name, uint32_t count, const char* fmt, ...);
    static ReplyPtr TryCmd(const std::string& name, uint32_t count, const std::vector<std::string>& args); 
};

}

#endif
