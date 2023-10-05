#include "cpp_redis.h"
#include "yhchaos/yhchaos.h"
#include "yhchaos/log.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
static yhchaos::AppConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_redis =
    yhchaos::AppConfig::SearchFor("redis.config", std::map<std::string, std::map<std::string, std::string> >(), "redis config");

static std::string get_value(const std::map<std::string, std::string>& m
                             ,const std::string& key
                             ,const std::string& def = "") {
    auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

redisReply* CppRedisReplyClone(redisReply* r) {
    redisReply* c = (redisReply*)calloc(1, sizeof(*c));
    c->type = r->type;

    switch(r->type) {
        case REDIS_REPLY_INTEGER:
            c->integer = r->integer;
            break;
        case REDIS_REPLY_ARRAY:
            if(r->element != NULL && r->elements > 0) {
                c->element = (redisReply**)calloc(r->elements, sizeof(r));
                c->elements = r->elements;
                for(size_t i = 0; i < r->elements; ++i) {
                    c->element[i] = CppRedisReplyClone(r->element[i]);
                }
            }
            break;
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_STRING:
            if(r->str == NULL) {
                c->str = NULL;
            } else {
                //c->str = strndup(r->str, r->len);
                c->str = (char*)malloc(r->len + 1);
                memcpy(c->str, r->str, r->len);
                c->str[r->len] = '\0';
            }
            c->len = r->len;
            break;
    }
    return c;
}

CppRedis::CppRedis() {
    m_type = ICppRedis::REDIS;
}

CppRedis::CppRedis(const std::map<std::string, std::string>& conf) {
    m_type = ICppRedis::REDIS;
    auto tmp = get_value(conf, "host");
    auto pos = tmp.find(":");
    m_host = tmp.substr(0, pos);
    m_port = yhchaos::TypeUtil::Atoi(tmp.substr(pos + 1));
    m_passwd = get_value(conf, "passwd");
    m_logEnable = yhchaos::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));

    tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = yhchaos::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}

bool CppRedis::reconnect() {
    return redisReconnect(m_context.get());
}

bool CppRedis::connect() {
    return connect(m_host, m_port, 50);
}

bool CppRedis::connect(const std::string& ip, int port, uint64_t ms) {
    m_host = ip;
    m_port = port;
    m_connectMs = ms;
    if(m_context) {
        return true;
    }
    timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    auto c = redisConnectWithTimeout(ip.c_str(), port, tv);
    if(c) {
        if(m_cmdTimeout.tv_sec || m_cmdTimeout.tv_usec) {
            setTimeout(m_cmdTimeout.tv_sec * 1000 + m_cmdTimeout.tv_usec / 1000);
        }
        m_context.reset(c, redisFree);

        if(!m_passwd.empty()) {
            auto r = (redisReply*)redisCommand(c, "auth %s", m_passwd.c_str());
            if(!r) {
                YHCHAOS_LOG_ERROR(g_logger) << "auth error:("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(r->type != REDIS_REPLY_STATUS) {
                YHCHAOS_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(!r->str) {
                YHCHAOS_LOG_ERROR(g_logger) << "auth reply str error: NULL("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(strcmp(r->str, "OK") == 0) {
                return true;
            } else {
                YHCHAOS_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
        }
        return true;
    }
    return false;
}

bool CppRedis::setTimeout(uint64_t v) {
    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
    redisSetTimeout(m_context.get(), m_cmdTimeout);
    return true;
}

ReplyPtr CppRedis::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = cmd(fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr CppRedis::cmd(const char* fmt, va_list ap) {
    auto r = (redisReply*)redisvCommand(m_context.get(), fmt, ap);
    if(!r) {
        if(m_logEnable) {
            YHCHAOS_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << ": " << r->str;
    }
    return nullptr;
}

ReplyPtr CppRedis::cmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }

    auto r = (redisReply*)redisCommandArgv(m_context.get(), argv.size(), &v[0], &l[0]);
    if(!r) {
        if(m_logEnable) {
            YHCHAOS_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << r->str;
    }
    return nullptr;
}

ReplyPtr CppRedis::getReply() {
    redisReply* r = nullptr;
    if(redisGetReply(m_context.get(), (void**)&r) == REDIS_OK) {
        ReplyPtr rt(r, freeReplyObject);
        return rt;
    }
    if(m_logEnable) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisGetReply error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
    }
    return nullptr;
}

int CppRedis::appendCmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rt = appendCmd(fmt, ap);
    va_end(ap);
    return rt;

}

int CppRedis::appendCmd(const char* fmt, va_list ap) {
    return redisvAppendCommand(m_context.get(), fmt, ap);
}

int CppRedis::appendCmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }
    return redisAppendCommandArgv(m_context.get(), argv.size(), &v[0], &l[0]);
}

CppRedisCluster::CppRedisCluster() {
    m_type = ICppRedis::REDIS_CLUSTER;
}

CppRedisCluster::CppRedisCluster(const std::map<std::string, std::string>& conf) {
    m_type = ICppRedis::REDIS_CLUSTER;
    m_host = get_value(conf, "host");
    m_passwd = get_value(conf, "passwd");
    m_logEnable = yhchaos::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));
    auto tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = yhchaos::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}


////CppRedisCluster
bool CppRedisCluster::reconnect() {
    return true;
    //return redisReconnect(m_context.get());
}

bool CppRedisCluster::connect() {
    return connect(m_host, m_port, 50);
}

bool CppRedisCluster::connect(const std::string& ip, int port, uint64_t ms) {
    m_host = ip;
    m_port = port;
    m_connectMs = ms;
    if(m_context) {
        return true;
    }
    timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    auto c = redisClusterConnectWithTimeout(ip.c_str(), tv, 0);
    if(c) {
        m_context.reset(c, redisClusterFree);
        if(!m_passwd.empty()) {
            auto r = (redisReply*)redisClusterCommand(c, "auth %s", m_passwd.c_str());
            if(!r) {
                YHCHAOS_LOG_ERROR(g_logger) << "auth error:("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(r->type != REDIS_REPLY_STATUS) {
                YHCHAOS_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(!r->str) {
                YHCHAOS_LOG_ERROR(g_logger) << "auth reply str error: NULL("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
            if(strcmp(r->str, "OK") == 0) {
                return true;
            } else {
                YHCHAOS_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
                    << m_host << ":" << m_port << ", " << m_name << ")";
                return false;
            }
        }
        return true;
    }
    return false;
}

bool CppRedisCluster::setTimeout(uint64_t ms) {
    //timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    //redisSetTimeout(m_context.get(), tv);
    return true;
}

ReplyPtr CppRedisCluster::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = cmd(fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr CppRedisCluster::cmd(const char* fmt, va_list ap) {
    auto r = (redisReply*)redisClustervCommand(m_context.get(), fmt, ap);
    if(!r) {
        if(m_logEnable) {
            YHCHAOS_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisCommand error: (" << fmt << ")(" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << ": " << r->str;
    }
    return nullptr;
}

ReplyPtr CppRedisCluster::cmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }

    auto r = (redisReply*)redisClusterCommandArgv(m_context.get(), argv.size(), &v[0], &l[0]);
    if(!r) {
        if(m_logEnable) {
            YHCHAOS_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if(r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if(m_logEnable) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisCommandArgv error: (" << m_host << ":" << m_port << ")(" << m_name << ")"
                    << r->str;
    }
    return nullptr;
}

ReplyPtr CppRedisCluster::getReply() {
    redisReply* r = nullptr;
    if(redisClusterGetReply(m_context.get(), (void**)&r) == REDIS_OK) {
        ReplyPtr rt(r, freeReplyObject);
        return rt;
    }
    if(m_logEnable) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisGetReply error: (" << m_host << ":" << m_port << ")(" << m_name << ")";
    }
    return nullptr;
}

int CppRedisCluster::appendCmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rt = appendCmd(fmt, ap);
    va_end(ap);
    return rt;

}

int CppRedisCluster::appendCmd(const char* fmt, va_list ap) {
    return redisClustervAppendCommand(m_context.get(), fmt, ap);
}

int CppRedisCluster::appendCmd(const std::vector<std::string>& argv) {
    std::vector<const char*> v;
    std::vector<size_t> l;
    for(auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }
    return redisClusterAppendCommandArg(m_context.get(), argv.size(), &v[0], &l[0]);
}

FoxCppRedis::FoxCppRedis(yhchaos::WatchCppThread* thr, const std::map<std::string, std::string>& conf)
    :m_thread(thr)
    ,m_status(UNCONNECTED)
    ,m_event(nullptr) {
    m_type = ICppRedis::FOX_REDIS;
    auto tmp = get_value(conf, "host");
    auto pos = tmp.find(":");
    m_host = tmp.substr(0, pos);
    m_port = yhchaos::TypeUtil::Atoi(tmp.substr(pos + 1));
    m_passwd = get_value(conf, "passwd");
    m_ctxCount = 0;
    m_logEnable = yhchaos::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));

    tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = yhchaos::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}

void FoxCppRedis::OnAuthCb(redisAsyncContext* c, void* rp, void* priv) {
    FoxCppRedis* fr = (FoxCppRedis*)priv;
    redisReply* r = (redisReply*)rp;
    if(!r) {
        YHCHAOS_LOG_ERROR(g_logger) << "auth error:("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
        return;
    }
    if(r->type != REDIS_REPLY_STATUS) {
        YHCHAOS_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
        return;
    }
    if(!r->str) {
        YHCHAOS_LOG_ERROR(g_logger) << "auth reply str error: NULL("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
        return;
    }
    if(strcmp(r->str, "OK") == 0) {
        YHCHAOS_LOG_INFO(g_logger) << "auth ok: " << r->str << "("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
    } else {
        YHCHAOS_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
            << fr->m_host << ":" << fr->m_port << ", " << fr->m_name << ")";
    }
}

void FoxCppRedis::ConnectCb(const redisAsyncContext* c, int status) {
    FoxCppRedis* ar = static_cast<FoxCppRedis*>(c->data);
    if(!status) {
        YHCHAOS_LOG_INFO(g_logger) << "FoxCppRedis::ConnectCb "
                   << c->c.tcp.host << ":" << c->c.tcp.port
                   << " success";
        ar->m_status = CONNECTED;
        if(!ar->m_passwd.empty()) {
            int rt = redisAsyncCommand(ar->m_context.get(), FoxCppRedis::OnAuthCb, ar, "auth %s", ar->m_passwd.c_str());
            if(rt) {
                YHCHAOS_LOG_ERROR(g_logger) << "FoxCppRedis Auth fail: " << rt;
            }
        }

    } else {
        YHCHAOS_LOG_ERROR(g_logger) << "FoxCppRedis::ConnectCb "
                    << c->c.tcp.host << ":" << c->c.tcp.port
                    << " fail, error:" << c->errstr;
        ar->m_status = UNCONNECTED;
    }
}

void FoxCppRedis::DisconnectCb(const redisAsyncContext* c, int status) {
    YHCHAOS_LOG_INFO(g_logger) << "FoxCppRedis::DisconnectCb "
               << c->c.tcp.host << ":" << c->c.tcp.port
               << " status:" << status;
    FoxCppRedis* ar = static_cast<FoxCppRedis*>(c->data);
    ar->m_status = UNCONNECTED;
}

void FoxCppRedis::CmdCb(redisAsyncContext *ac, void *r, void *privdata) {
    Ctx* ctx = static_cast<Ctx*>(privdata);
    if(!ctx) {
        return;
    }
    if(ctx->timeout) {
        delete ctx;
        //if(ctx && ctx->coroutine) {
        //    YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd << "' timeout("
        //                << (ctx->rds->m_cmdTimeout.tv_sec * 1000
        //                        + ctx->rds->m_cmdTimeout.tv_usec / 1000)
        //                << "ms)";
        //    ctx->coscheduler->coschedule(&ctx->coroutine);
        //    ctx->cancelFdEvent();
        //}
        return;
    }

    auto m_logEnable = ctx->rds->m_logEnable;

    redisReply* reply = (redisReply*)r;
    if(ac->err) {
        if(m_logEnable) {
            yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "(" << ac->err << ") " << ac->errstr;
        }
        if(ctx->fctx->coroutine) {
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    } else if(!reply) {
        if(m_logEnable) {
            yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: NULL";
        }
        if(ctx->fctx->coroutine) {
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    } else if(reply->type == REDIS_REPLY_ERROR) {
        if(m_logEnable) {
            yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: " << reply->str;
        }
        if(ctx->fctx->coroutine) {
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    } else {
        if(ctx->fctx->coroutine) {
            ctx->fctx->rpy.reset(CppRedisReplyClone(reply), freeReplyObject);
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    }
    ctx->cancelFdEvent();
    delete ctx;
}
//static函数
void FoxCppRedis::TimeCb(int fd, short event, void* d) {
    FoxCppRedis* ar = static_cast<FoxCppRedis*>(d);
    redisAsyncCommand(ar->m_context.get(), CmdCb, nullptr, "ping");
}

struct Res {
    redisAsyncContext* ctx;
    struct event* event;
};

//void DelayTimeCb(int fd, short event, void* d) {
//    YHCHAOS_LOG_INFO(g_logger) << "DelayTimeCb";
//    Res* res = static_cast<Res*>(d);
//    redisAsyncFree(res->ctx);
//    evtimer_del(res->event);
//    event_free(res->event);
//    delete res;
//}

bool FoxCppRedis::init() {
    //在创建m_thread的线程中执行init，创建的时候提供了event_base
    if(m_thread == yhchaos::WatchCppThread::GetThis()) {
        return pinit();
    } else {
        m_thread->dispatch(std::bind(&FoxCppRedis::pinit, this));
    }
    return true;
}

void FoxCppRedis::delayDelete(redisAsyncContext* c) {
    //if(!c) {
    //    return;
    //}

    //Res* res = new Res();
    //res->ctx = c;
    //struct event* event = event_new(m_thread->getBase(), -1, EV_TIMEOUT, DelayTimeCb, res);
    //res->event = event;
    //
    //struct timeval tv = {60, 0};
    //evtimer_add(event, &tv);
}

bool FoxCppRedis::pinit() {
    //YHCHAOS_LOG_INFO(g_logger) << "pinit m_status=" << m_status;
    if(m_status != UNCONNECTED) {
        return true;
    }
    auto ctx = redisAsyncConnect(m_host.c_str(), m_port);
    if(!ctx) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisAsyncConnect (" << m_host << ":" << m_port
                    << ") null";
        return false;
    }
    if(ctx->err) {
        YHCHAOS_LOG_ERROR(g_logger) << "Error:(" << ctx->err << ")" << ctx->errstr;
        return false;
    }
    ctx->data = this;
    //让redis的事件在m_thread创建的线程中进行监听，当事件发生时调用相应的处理函数，这里是ConnectCb
    redisLibeventAttach(ctx, m_thread->getBase());
    redisAsyncSetConnectCallback(ctx, ConnectCb);
    redisAsyncSetDisconnectCallback(ctx, DisconnectCb);
    m_status = CONNECTING;
    //m_context.reset(ctx, redisAsyncFree);
    m_context.reset(ctx, yhchaos::nop<redisAsyncContext>);
    //m_context.reset(ctx, std::bind(&FoxCppRedis::delayDelete, this, std::placeholders::_1));
    if(m_event == nullptr) {
        m_event = event_new(m_thread->getBase(), -1, EV_TIMEOUT | EV_PERSIST, TimeCb, this);
        struct timeval tv = {120, 0};
        evtimer_add(m_event, &tv);
    }
    TimeCb(0, 0, this);
    return true;
}

ReplyPtr FoxCppRedis::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto r = cmd(fmt, ap);
    va_end(ap);
    return r;
}

ReplyPtr FoxCppRedis::cmd(const char* fmt, va_list ap) {
    char* buf = nullptr;
    //int len = vasprintf(&buf, fmt, ap);
    int len = redisvFormatCommand(&buf, fmt, ap);
    if(len == -1) {
        YHCHAOS_LOG_ERROR(g_logger) << "redis fmt error: " << fmt;
        return nullptr;
    }
    //Ctx::ptr ctx(new Ctx(this));
    //if(buf) {
    //    ctx->cmd.append(buf, len);
    //    free(buf);
    //}
    //ctx->coscheduler = yhchaos::CoScheduler::GetThis();
    //ctx->coroutine = yhchaos::Coroutine::GetThis();
    //ctx->thread = m_thread;

    FCtx fctx;
    fctx.cmd.append(buf, len);
    free(buf);
    fctx.coscheduler = yhchaos::CoScheduler::GetThis();
    fctx.coroutine = yhchaos::Coroutine::GetThis();

    m_thread->dispatch(std::bind(&FoxCppRedis::pcmd, this, &fctx));
    //返回到主协程中执行
    yhchaos::Coroutine::YieldToHold();
    return fctx.rpy;
}

ReplyPtr FoxCppRedis::cmd(const std::vector<std::string>& argv) {
    //Ctx::ptr ctx(new Ctx(this));
    //ctx->parts = argv;
    FCtx fctx;
    do {
        std::vector<const char*> args;
        std::vector<size_t> args_len;
        for(auto& i : argv) {
            args.push_back(i.c_str());
            args_len.push_back(i.size());
        }
        char* buf = nullptr;
        //构建redis命令，写入buff
        int len = redisFormatCommandArgv(&buf, argv.size(), &(args[0]), &(args_len[0]));
        if(len == -1 || !buf) {
            YHCHAOS_LOG_ERROR(g_logger) << "redis fmt error";
            return nullptr;
        }
        fctx.cmd.append(buf, len);
        free(buf);
    } while(0);

    //ctx->coscheduler = yhchaos::CoScheduler::GetThis();
    //ctx->coroutine = yhchaos::Coroutine::GetThis();
    //ctx->thread = m_thread;

    fctx.coscheduler = yhchaos::CoScheduler::GetThis();
    //设置为当前线程的主协程
    fctx.coroutine = yhchaos::Coroutine::GetThis();
    //将命令发送出去，知道收到结果的时候才返回这个协程继续执行，期间可以执行别的协程
    m_thread->dispatch(std::bind(&FoxCppRedis::pcmd, this, &fctx));
    yhchaos::Coroutine::YieldToHold();
    return fctx.rpy;
}

void FoxCppRedis::pcmd(FCtx* fctx) {
    if(m_status == UNCONNECTED) {
        YHCHAOS_LOG_INFO(g_logger) << "redis (" << m_host << ":" << m_port << ") unconnected "
                   << fctx->cmd;
        init();
        if(fctx->coroutine) {
            fctx->coscheduler->coschedule(&fctx->coroutine);
        }
        return;
    }
    //构建一个ctx对象
    Ctx* ctx(new Ctx(this));
    ctx->thread = m_thread;
    ctx->init();
    ctx->fctx = fctx;
    ctx->cmd = fctx->cmd;

    if(!ctx->cmd.empty()) {
        //redisAsyncCommand(m_context.get(), CmdCb, ctx.get(), ctx->cmd.c_str());
        redisAsyncFormattedCommand(m_context.get(), CmdCb, ctx, ctx->cmd.c_str(), ctx->cmd.size());
    //} else if(!ctx->parts.empty()) {
    //    std::vector<const char*> argv;
    //    std::vector<size_t> argv_len;
    //    for(auto& i : ctx->parts) {
    //        argv.push_back(i.c_str());
    //        argv_len.push_back(i.size());
    //    }
    //    redisAsyncCommandArgv(m_context.get(), CmdCb, ctx.get(), argv.size(),
    //            &(argv[0]), &(argv_len[0]));
    }
}

FoxCppRedis::~FoxCppRedis() {
    if(m_event) {
        evtimer_del(m_event);
        event_free(m_event);
    }
}

FoxCppRedis::Ctx::Ctx(FoxCppRedis* r)
    :ev(nullptr)
    ,timeout(false)
    ,rds(r)
    //,coscheduler(nullptr)
    ,thread(nullptr) {
    yhchaos::Atomic::addFetch(rds->m_ctxCount, 1);
}

FoxCppRedis::Ctx::~Ctx() {
    //cancelFdEvent();
    YHCHAOS_ASSERT(thread == yhchaos::WatchCppThread::GetThis());
    //YHCHAOS_ASSERT(destory == 0);
    yhchaos::Atomic::subFetch(rds->m_ctxCount, 1);
    //++destory;
    //cancelFdEvent();
    if(ev) {
        evtimer_del(ev);
        event_free(ev);
        ev = nullptr;
    }

}

void FoxCppRedis::Ctx::cancelFdEvent() {
    //if(ev) {
    //    if(thread == yhchaos::WatchCppThread::GetThis()) {
    //        evtimer_del(ev);
    //        event_free(ev);
    //    } else {
    //        auto e = ev;
    //        thread->dispatch([e](){
    //            evtimer_del(e);
    //            event_free(e);
    //        });
    //    }
    //    ev = nullptr;
    //}
    //ref = nullptr;
}

bool FoxCppRedis::Ctx::init() {
    ev = evtimer_new(rds->m_thread->getBase(), FdEventCb, this);
    evtimer_add(ev, &rds->m_cmdTimeout);
    return true;
}

void FoxCppRedis::Ctx::FdEventCb(int fd, short event, void* d) {
    Ctx* ctx = static_cast<Ctx*>(d);
    ctx->timeout = 1;
    if(ctx->rds->m_logEnable) {
        yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
        YHCHAOS_LOG_INFO(g_logger) << "redis cmd: '" << ctx->cmd << "' reach timeout "
                   << (ctx->rds->m_cmdTimeout.tv_sec * 1000
                           + ctx->rds->m_cmdTimeout.tv_usec / 1000) << "ms";
    }
    if(ctx->fctx->coroutine) {
        ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
    }
    ctx->cancelFdEvent();
    //ctx->ref = nullptr;
}

FoxCppRedisCluster::FoxCppRedisCluster(yhchaos::WatchCppThread* thr, const std::map<std::string, std::string>& conf)
    :m_thread(thr)
    ,m_status(UNCONNECTED)
    ,m_event(nullptr) {
    m_ctxCount = 0;

    m_type = ICppRedis::FOX_REDIS_CLUSTER;
    m_host = get_value(conf, "host");
    m_passwd = get_value(conf, "passwd");
    m_logEnable = yhchaos::TypeUtil::Atoi(get_value(conf, "log_enable", "1"));
    auto tmp = get_value(conf, "timeout_com");
    if(tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = yhchaos::TypeUtil::Atoi(tmp);

    m_cmdTimeout.tv_sec = v / 1000;
    m_cmdTimeout.tv_usec = v % 1000 * 1000;
}

void FoxCppRedisCluster::OnAuthCb(redisClusterAsyncContext* c, void* rp, void* priv) {
    FoxCppRedisCluster* fr = (FoxCppRedisCluster*)priv;
    redisReply* r = (redisReply*)rp;
    if(!r) {
        YHCHAOS_LOG_ERROR(g_logger) << "auth error:("
            << fr->m_host << ", " << fr->m_name << ")";
        return;
    }
    if(r->type != REDIS_REPLY_STATUS) {
        YHCHAOS_LOG_ERROR(g_logger) << "auth reply type error:" << r->type << "("
            << fr->m_host << ", " << fr->m_name << ")";
        return;
    }
    if(!r->str) {
        YHCHAOS_LOG_ERROR(g_logger) << "auth reply str error: NULL("
            << fr->m_host << ", " << fr->m_name << ")";
        return;
    }
    if(strcmp(r->str, "OK") == 0) {
        YHCHAOS_LOG_INFO(g_logger) << "auth ok: " << r->str << "("
            << fr->m_host << ", " << fr->m_name << ")";
    } else {
        YHCHAOS_LOG_ERROR(g_logger) << "auth error: " << r->str << "("
            << fr->m_host << ", " << fr->m_name << ")";
    }
}

void FoxCppRedisCluster::ConnectCb(const redisAsyncContext* c, int status) {
    FoxCppRedisCluster* ar = static_cast<FoxCppRedisCluster*>(c->data);
    if(!status) {
        YHCHAOS_LOG_INFO(g_logger) << "FoxCppRedisCluster::ConnectCb "
                   << c->c.tcp.host << ":" << c->c.tcp.port
                   << " success";
        if(!ar->m_passwd.empty()) {
            int rt = redisClusterAsyncCommand(ar->m_context.get(), FoxCppRedisCluster::OnAuthCb, ar, "auth %s", ar->m_passwd.c_str());
            if(rt) {
                YHCHAOS_LOG_ERROR(g_logger) << "FoxCppRedisCluster Auth fail: " << rt;
            }
        }
    } else {
        YHCHAOS_LOG_ERROR(g_logger) << "FoxCppRedisCluster::ConnectCb "
                    << c->c.tcp.host << ":" << c->c.tcp.port
                    << " fail, error:" << c->errstr;
    }
}

void FoxCppRedisCluster::DisconnectCb(const redisAsyncContext* c, int status) {
    YHCHAOS_LOG_INFO(g_logger) << "FoxCppRedisCluster::DisconnectCb "
               << c->c.tcp.host << ":" << c->c.tcp.port
               << " status:" << status;
}

void FoxCppRedisCluster::CmdCb(redisClusterAsyncContext *ac, void *r, void *privdata) {
    Ctx* ctx = static_cast<Ctx*>(privdata);
    if(ctx->timeout) {
        delete ctx;
        //if(ctx && ctx->coroutine) {
        //    YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd << "' timeout("
        //                << (ctx->rds->m_cmdTimeout.tv_sec * 1000
        //                        + ctx->rds->m_cmdTimeout.tv_usec / 1000)
        //                << "ms)";
        //    ctx->coscheduler->coschedule(&ctx->coroutine);
        //    ctx->cancelFdEvent();
        //}
        return;
    }
    auto m_logEnable = ctx->rds->m_logEnable;
    //ctx->cancelFdEvent();
    redisReply* reply = (redisReply*)r;
    //++ctx->callback_count;
    if(ac->err) {
        if(m_logEnable) {
            yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "(" << ac->err << ") " << ac->errstr;
        }
        if(ctx->fctx->coroutine) {
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    } else if(!reply) {
        if(m_logEnable) {
            yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: NULL";
        }
        if(ctx->fctx->coroutine) {
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    } else if(reply->type == REDIS_REPLY_ERROR) {
        if(m_logEnable) {
            yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
            YHCHAOS_LOG_ERROR(g_logger) << "redis cmd: '" << ctx->cmd
                        << "' "
                        << "reply: " << reply->str;
        }
        if(ctx->fctx->coroutine) {
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    } else {
        if(ctx->fctx->coroutine) {
            ctx->fctx->rpy.reset(CppRedisReplyClone(reply), freeReplyObject);
            ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
        }
    }
    //ctx->ref = nullptr;
    delete ctx;
    //ctx->tref = nullptr;
}

void FoxCppRedisCluster::TimeCb(int fd, short event, void* d) {
    //FoxCppRedisCluster* ar = static_cast<FoxCppRedisCluster*>(d);
    //redisAsyncCommand(ar->m_context.get(), CmdCb, nullptr, "ping");
}

bool FoxCppRedisCluster::init() {
    if(m_thread == yhchaos::WatchCppThread::GetThis()) {
        return pinit();
    } else {
        m_thread->dispatch(std::bind(&FoxCppRedisCluster::pinit, this));
    }
    return true;
}

void FoxCppRedisCluster::delayDelete(redisAsyncContext* c) {
    //if(!c) {
    //    return;
    //}

    //Res* res = new Res();
    //res->ctx = c;
    //struct event* event = event_new(m_thread->getBase(), -1, EV_TIMEOUT, DelayTimeCb, res);
    //res->event = event;
    //
    //struct timeval tv = {60, 0};
    //evtimer_add(event, &tv);
}

bool FoxCppRedisCluster::pinit() {
    if(m_status != UNCONNECTED) {
        return true;
    }
    YHCHAOS_LOG_INFO(g_logger) << "FoxCppRedisCluster pinit:" << m_host;
    auto ctx = redisClusterAsyncConnect(m_host.c_str(), 0);
    ctx->data = this;
    redisClusterLibeventAttach(ctx, m_thread->getBase());
    redisClusterAsyncSetConnectCallback(ctx, ConnectCb);
    redisClusterAsyncSetDisconnectCallback(ctx, DisconnectCb);
    if(!ctx) {
        YHCHAOS_LOG_ERROR(g_logger) << "redisClusterAsyncConnect (" << m_host
                    << ") null";
        return false;
    }
    if(ctx->err) {
        YHCHAOS_LOG_ERROR(g_logger) << "Error:(" << ctx->err << ")" << ctx->errstr
            << " passwd=" << m_passwd;
        return false;
    }
    m_status = CONNECTED;
    //m_context.reset(ctx, redisAsyncFree);
    m_context.reset(ctx, yhchaos::nop<redisClusterAsyncContext>);
    //m_context.reset(ctx, std::bind(&FoxCppRedisCluster::delayDelete, this, std::placeholders::_1));
    if(m_event == nullptr) {
        m_event = event_new(m_thread->getBase(), -1, EV_TIMEOUT | EV_PERSIST, TimeCb, this);
        struct timeval tv = {120, 0};
        evtimer_add(m_event, &tv);
        TimeCb(0, 0, this);
    }
    return true;
}

ReplyPtr FoxCppRedisCluster::cmd(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto r = cmd(fmt, ap);
    va_end(ap);
    return r;
}

ReplyPtr FoxCppRedisCluster::cmd(const char* fmt, va_list ap) {
    char* buf = nullptr;
    //int len = vasprintf(&buf, fmt, ap);
    int len = redisvFormatCommand(&buf, fmt, ap);
    if(len == -1 || !buf) {
        YHCHAOS_LOG_ERROR(g_logger) << "redis fmt error: " << fmt;
        return nullptr;
    }
    FCtx fctx;
    fctx.cmd.append(buf, len);
    free(buf);
    fctx.coscheduler = yhchaos::CoScheduler::GetThis();
    fctx.coroutine = yhchaos::Coroutine::GetThis();
    //Ctx::ptr ctx(new Ctx(this));
    //if(buf) {
    //    ctx->cmd.append(buf, len);
    //    free(buf);
    //}
    //ctx->coscheduler = yhchaos::CoScheduler::GetThis();
    //ctx->coroutine = yhchaos::Coroutine::GetThis();
    //ctx->thread = m_thread;

    m_thread->dispatch(std::bind(&FoxCppRedisCluster::pcmd, this, &fctx));
    yhchaos::Coroutine::YieldToHold();
    return fctx.rpy;
}

ReplyPtr FoxCppRedisCluster::cmd(const std::vector<std::string>& argv) {
    //Ctx::ptr ctx(new Ctx(this));
    //ctx->parts = argv;
    FCtx fctx;
    do {
        std::vector<const char*> args;
        std::vector<size_t> args_len;
        for(auto& i : argv) {
            args.push_back(i.c_str());
            args_len.push_back(i.size());
        }
        char* buf = nullptr;
        int len = redisFormatCommandArgv(&buf, argv.size(), &(args[0]), &(args_len[0]));
        if(len == -1 || !buf) {
            YHCHAOS_LOG_ERROR(g_logger) << "redis fmt error";
            return nullptr;
        }
        fctx.cmd.append(buf, len);
        free(buf);
    } while(0);

    fctx.coscheduler = yhchaos::CoScheduler::GetThis();
    fctx.coroutine = yhchaos::Coroutine::GetThis();

    m_thread->dispatch(std::bind(&FoxCppRedisCluster::pcmd, this, &fctx));
    yhchaos::Coroutine::YieldToHold();
    return fctx.rpy;
}

void FoxCppRedisCluster::pcmd(FCtx* fctx) {
    if(m_status != CONNECTED) {
        YHCHAOS_LOG_INFO(g_logger) << "redis (" << m_host << ") unconnected "
                   << fctx->cmd;
        init();
        if(fctx->coroutine) {
            fctx->coscheduler->coschedule(&fctx->coroutine);
        }
        return;
    }
    Ctx* ctx(new Ctx(this));
    ctx->thread = m_thread;
    ctx->init();
    ctx->fctx = fctx;
    ctx->cmd = fctx->cmd;
    //ctx->ref = ctx;
    //ctx->tref = ctx;
    if(!ctx->cmd.empty()) {
        //redisClusterAsyncCommand(m_context.get(), CmdCb, ctx.get(), ctx->cmd.c_str());
        redisClusterAsyncFormattedCommand(m_context.get(), CmdCb, ctx, &ctx->cmd[0], ctx->cmd.size());
    //} else if(!ctx->parts.empty()) {
    //    std::vector<const char*> argv;
    //    std::vector<size_t> argv_len;
    //    for(auto& i : ctx->parts) {
    //        argv.push_back(i.c_str());
    //        argv_len.push_back(i.size());
    //    }
    //    redisClusterAsyncCommandArgv(m_context.get(), CmdCb, ctx.get(), argv.size(),
    //            &(argv[0]), &(argv_len[0]));
    }
}

FoxCppRedisCluster::~FoxCppRedisCluster() {
    if(m_event) {
        evtimer_del(m_event);
        event_free(m_event);
    }
}

FoxCppRedisCluster::Ctx::Ctx(FoxCppRedisCluster* r)
    :ev(nullptr)
    ,timeout(false)
    ,rds(r)
    //,coscheduler(nullptr)
    ,thread(nullptr) {
    //,cancel_count(0)
    //,destory(0)
    //,callback_count(0) {
    fctx = nullptr;
    yhchaos::Atomic::addFetch(rds->m_ctxCount, 1);
}

FoxCppRedisCluster::Ctx::~Ctx() {
    YHCHAOS_ASSERT(thread == yhchaos::WatchCppThread::GetThis());
    //YHCHAOS_ASSERT(destory == 0);
    yhchaos::Atomic::subFetch(rds->m_ctxCount, 1);
    //++destory;
    //cancelFdEvent();

    if(ev) {
        evtimer_del(ev);
        event_free(ev);
        ev = nullptr;
    }
}

void FoxCppRedisCluster::Ctx::cancelFdEvent() {
    //YHCHAOS_LOG_INFO(g_logger) << "cancelFdEvent " << yhchaos::WatchCppThread::GetThis()
    //           << " - " << thread
    //           << " - " << yhchaos::IOCoScheduler::GetThis()
    //           << " - " << cancel_count;
    //if(thread != yhchaos::WatchCppThread::GetThis()) {
    //    YHCHAOS_LOG_INFO(g_logger) << "cancelFdEvent " << yhchaos::WatchCppThread::GetThis()
    //               << " - " << thread
    //               << " - " << yhchaos::IOCoScheduler::GetThis()
    //               << " - " << cancel_count;

    //    //YHCHAOS_LOG_INFO(g_logger) << "cancelFdEvent thread=" << thread << " " << thread->getId()
    //    //           << " this=" << yhchaos::WatchCppThread::GetThis();
    //    //YHCHAOS_ASSERT(thread == yhchaos::WatchCppThread::GetThis());
    //}
    //YHCHAOS_ASSERT(!yhchaos::IOCoScheduler::GetThis());
    ////if(yhchaos::Atomic::addFetch(cancel_count) > 1) {
    ////    return;
    ////}
    ////YHCHAOS_ASSERT(!yhchaos::Coroutine::GetThis());
    ////yhchaos::RWMtx::WriteLock lock(mutex);
    //if(++cancel_count > 1) {
    //    return;
    //}
    //if(ev) {
    //    auto e = ev;
    //    ev = nullptr;
    //    //lock.unlock();
    //    //evtimer_del(e);
    //    //event_free(e);
    //    if(thread == yhchaos::WatchCppThread::GetThis()) {
    //        evtimer_del(e);
    //        event_free(e);
    //    } else {
    //        thread->dispatch([e](){
    //            evtimer_del(e);
    //            event_free(e);
    //        });
    //    }
    //}
    //ref = nullptr;
}

bool FoxCppRedisCluster::Ctx::init() {
    YHCHAOS_ASSERT(thread == yhchaos::WatchCppThread::GetThis());
    ev = evtimer_new(rds->m_thread->getBase(), FdEventCb, this);
    evtimer_add(ev, &rds->m_cmdTimeout);
    return true;
}

void FoxCppRedisCluster::Ctx::FdEventCb(int fd, short event, void* d) {
    Ctx* ctx = static_cast<Ctx*>(d);
    if(!ctx->ev) {
        return;
    }
    ctx->timeout = 1;
    if(ctx->rds->m_logEnable) {
        yhchaos::replace(ctx->cmd, "\r\n", "\\r\\n");
        YHCHAOS_LOG_INFO(g_logger) << "redis cmd: '" << ctx->cmd << "' reach timeout "
                   << (ctx->rds->m_cmdTimeout.tv_sec * 1000
                           + ctx->rds->m_cmdTimeout.tv_usec / 1000) << "ms";
    }
    ctx->cancelFdEvent();
    if(ctx->fctx->coroutine) {
        ctx->fctx->coscheduler->coschedule(&ctx->fctx->coroutine);
    }
    //ctx->ref = nullptr;
    //delete ctx;
    //ctx->tref = nullptr;
}

ICppRedis::ptr CppRedisManager::get(const std::string& name) {
    yhchaos::RWMtx::WriteLock lock(m_mutex);
    auto it = m_datas.find(name);
    if(it == m_datas.end()) {
        return nullptr;
    }
    if(it->second.empty()) {
        return nullptr;
    }
    auto r = it->second.front();
    it->second.pop_front();
    if(r->getType() == ICppRedis::FOX_REDIS
            || r->getType() == ICppRedis::FOX_REDIS_CLUSTER) {
        it->second.push_back(r);
        return std::shared_ptr<ICppRedis>(r, yhchaos::nop<ICppRedis>);
    }
    lock.unlock();
    auto rr = dynamic_cast<ISyncCppRedis*>(r);
    if((time(0) - rr->getLastActiveTime()) > 30) {
        if(!rr->cmd("ping")) {
            if(!rr->reconnect()) {
                yhchaos::RWMtx::WriteLock lock(m_mutex);
                m_datas[name].push_back(r);
                return nullptr;
            }
        }
    }
    rr->setLastActiveTime(time(0));
    return std::shared_ptr<ICppRedis>(r, std::bind(&CppRedisManager::freeCppRedis
                        ,this, std::placeholders::_1));
}

void CppRedisManager::freeCppRedis(ICppRedis* r) {
    yhchaos::RWMtx::WriteLock lock(m_mutex);
    m_datas[r->getName()].push_back(r);
}

CppRedisManager::CppRedisManager() {
    init();
}

void CppRedisManager::init() {
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
    //redis.config
    m_config = g_redis->getValue();
    size_t done = 0;
    size_t total = 0;
    for(auto& i : m_config) {
        auto type = get_value(i.second, "type");
        auto pool = yhchaos::TypeUtil::Atoi(get_value(i.second, "pool"));
        auto passwd = get_value(i.second, "passwd");
        total += pool;
        for(int n = 0; n < pool; ++n) {
            if(type == "redis") {
                yhchaos::CppRedis* rds(new yhchaos::CppRedis(i.second));
                rds->connect();
                rds->setLastActiveTime(time(0));
                yhchaos::RWMtx::WriteLock lock(m_mutex);
                m_datas[i.first].push_back(rds);
                yhchaos::Atomic::addFetch(done, 1);
            } else if(type == "redis_cluster") {
                yhchaos::CppRedisCluster* rds(new yhchaos::CppRedisCluster(i.second));
                rds->connect();
                rds->setLastActiveTime(time(0));
                yhchaos::RWMtx::WriteLock lock(m_mutex);
                m_datas[i.first].push_back(rds);
                yhchaos::Atomic::addFetch(done, 1);
            } else if(type == "fox_redis") {
                auto conf = i.second;
                auto name = i.first;
                yhchaos::WatchCppThreadMgr::GetInstance()->dispatch("redis", [this, conf, name, &done](){
                    yhchaos::FoxCppRedis* rds(new yhchaos::FoxCppRedis(yhchaos::WatchCppThread::GetThis(), conf));
                    rds->init();
                    rds->setName(name);

                    yhchaos::RWMtx::WriteLock lock(m_mutex);
                    m_datas[name].push_back(rds);
                    yhchaos::Atomic::addFetch(done, 1);
                });
            } else if(type == "fox_redis_cluster") {
                auto conf = i.second;
                auto name = i.first;
                yhchaos::WatchCppThreadMgr::GetInstance()->dispatch("redis", [this, conf, name, &done](){
                    yhchaos::FoxCppRedisCluster* rds(new yhchaos::FoxCppRedisCluster(yhchaos::WatchCppThread::GetThis(), conf));
                    rds->init();
                    rds->setName(name);

                    yhchaos::RWMtx::WriteLock lock(m_mutex);
                    m_datas[name].push_back(rds);
                    yhchaos::Atomic::addFetch(done, 1);
                });
            } else {
                yhchaos::Atomic::addFetch(done, 1);
            }
        }
    }

    while(done != total) {
        usleep(5000);
    }
}

std::ostream& CppRedisManager::dump(std::ostream& os) {
    os << "[CppRedisManager total=" << m_config.size() << "]" << std::endl;
    for(auto& i : m_config) {
        os << "    " << i.first << " :[";
        for(auto& n : i.second) {
            os << "{" << n.first << ":" << n.second << "}";
        }
        os << "]" << std::endl;
    }
    return os;
}

ReplyPtr CppRedisUtil::Cmd(const std::string& name, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = Cmd(name, fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr CppRedisUtil::Cmd(const std::string& name, const char* fmt, va_list ap) {
    auto rds = CppRedisMgr::GetInstance()->get(name);
    if(!rds) {
        return nullptr;
    }
    return rds->cmd(fmt, ap);
}

ReplyPtr CppRedisUtil::Cmd(const std::string& name, const std::vector<std::string>& args) {
    auto rds = CppRedisMgr::GetInstance()->get(name);
    if(!rds) {
        return nullptr;
    }
    return rds->cmd(args);
}


ReplyPtr CppRedisUtil::TryCmd(const std::string& name, uint32_t count, const char* fmt, ...) {
    for(uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, fmt);
        ReplyPtr rt = Cmd(name, fmt, ap);
        va_end(ap);

        if(rt) {
            return rt;
        }
    }
    return nullptr;
}

ReplyPtr CppRedisUtil::TryCmd(const std::string& name, uint32_t count, const std::vector<std::string>& args) {
    for(uint32_t i = 0; i < count; ++i) {
        ReplyPtr rt = Cmd(name, args);
        if(rt) {
            return rt;
        }
    }
    return nullptr;
}

}
