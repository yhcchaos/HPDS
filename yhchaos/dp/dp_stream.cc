#include "dp_stream.h"
#include "yhchaos/log.h"
#include "yhchaos/appconfig.h"
#include "yhchaos/worker.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
static yhchaos::AppConfigVar<std::unordered_map<std::string
    ,std::unordered_map<std::string, std::string> > >::ptr g_dp_services =
    yhchaos::AppConfig::SearchFor("dp_services", std::unordered_map<std::string
    ,std::unordered_map<std::string, std::string> >(), "dp_services");

//static yhchaos::AppConfigVar<std::unordered_map<std::string
//    ,std::unordered_map<std::string, std::string> > >::ptr g_dp_services =
//    yhchaos::AppConfig::SearchFor("dp_services", std::unordered_map<std::string
//    ,std::unordered_map<std::string, std::string> >(), "dp_services");

std::string DPRes::toString() const {
    std::stringstream ss;
    ss << "[DPRes result=" << result
       << " used=" << used
       << " response=" << (response ? response->toString() : "null")
       << " request=" << (request ? request->toString() : "null")
       << "]";
    return ss.str();
}

DPStream::DPStream(Sock::ptr sock)
    :AsyncSockStream(sock, true)
    ,m_decoder(new DPMSGDecoder) {
    YHCHAOS_LOG_DEBUG(g_logger) << "DPStream::DPStream "
        << this << " "
        << (sock ? sock->toString() : "");
}

DPStream::~DPStream() {
    YHCHAOS_LOG_DEBUG(g_logger) << "DPStream::~DPStream "
        << this << " "
        << (m_socket ? m_socket->toString() : "");
}

int32_t DPStream::sendMSG(MSG::ptr msg) {
    if(isConnected()) {
        DPSendCtx::ptr ctx(new DPSendCtx);
        ctx->msg = msg;
        enqueue(ctx);
        return 1;
    } else {
        return -1; 
    }
}

DPRes::ptr DPStream::request(DPReq::ptr req, uint32_t timeout_ms) {
    if(isConnected()) {
        DPCtx::ptr ctx(new DPCtx);
        ctx->request = req;
        ctx->sn = req->getSn();
        ctx->timeout = timeout_ms;
        ctx->coscheduler = yhchaos::CoScheduler::GetThis();
        ctx->coroutine = yhchaos::Coroutine::GetThis();
        addCtx(ctx);
        uint64_t ts = yhchaos::GetCurrentMS();
        ctx->timer = yhchaos::IOCoScheduler::GetThis()->addTimedCoroutine(timeout_ms,
                std::bind(&DPStream::onTimeOut, shared_from_this(), ctx));
        enqueue(ctx);
        yhchaos::Coroutine::YieldToHold();
        return std::make_shared<DPRes>(ctx->result, yhchaos::GetCurrentMS() - ts, ctx->response, req);
    } else {
        return std::make_shared<DPRes>(AsyncSockStream::NOT_CONNECT, 0, nullptr, req);
    }
}

bool DPStream::DPSendCtx::doSend(AsyncSockStream::ptr stream) {
    return std::dynamic_pointer_cast<DPStream>(stream)
                ->m_decoder->serializeTo(stream, msg) > 0;
}

bool DPStream::DPCtx::doSend(AsyncSockStream::ptr stream) {
    return std::dynamic_pointer_cast<DPStream>(stream)
                ->m_decoder->serializeTo(stream, request) > 0;
}

AsyncSockStream::Ctx::ptr DPStream::doRecv() {
    //YHCHAOS_LOG_INFO(g_logger) << "doRecv " << this;
    auto msg = m_decoder->parseFrom(shared_from_this());
    if(!msg) {
        innerClose();
        return nullptr;
    }

    int type = msg->getType();
    if(type == MSG::RESPONSE) {
        auto rsp = std::dynamic_pointer_cast<DPRsp>(msg);
        if(!rsp) {
            YHCHAOS_LOG_WARN(g_logger) << "DPStream doRecv response not DPRsp: "
                << msg->toString();
            return nullptr;
        }
        //从m_ctxs中取出sn对应的ctx，然后从m_ctxs中删除,再将ctx转换为dpctx
        DPCtx::ptr ctx = getAndDelCtxAs<DPCtx>(rsp->getSn());
        if(!ctx) {
            YHCHAOS_LOG_WARN(g_logger) << "DPStream request timeout reponse="
                << rsp->toString();
            return nullptr;
        }
        ctx->result = rsp->getRes();
        ctx->response = rsp;
        return ctx;
    } else if(type == MSG::REQUEST) {
        auto req = std::dynamic_pointer_cast<DPReq>(msg);
        if(!req) {
            YHCHAOS_LOG_WARN(g_logger) << "DPStream doRecv request not DPReq: "
                << msg->toString();
            return nullptr;
        }
        if(m_requestHandler) {
            m_worker->coschedule(std::bind(&DPStream::handleReq,
                        std::dynamic_pointer_cast<DPStream>(shared_from_this()),
                        req));
        } else {
            YHCHAOS_LOG_WARN(g_logger) << "unhandle request " << req->toString();
        }
    } else if(type == MSG::NOTIFY) {
        auto nty = std::dynamic_pointer_cast<DPNotify>(msg);
        if(!nty) {
            YHCHAOS_LOG_WARN(g_logger) << "DPStream doRecv notify not DPNotify: "
                << msg->toString();
            return nullptr;
        }

        if(m_notifyHandler) {
            m_worker->coschedule(std::bind(&DPStream::handleNotify,
                        std::dynamic_pointer_cast<DPStream>(shared_from_this()),
                        nty));
        } else {
            YHCHAOS_LOG_WARN(g_logger) << "unhandle notify " << nty->toString();
        }
    } else {
        YHCHAOS_LOG_WARN(g_logger) << "DPStream recv unknow type=" << type
            << " msg: " << msg->toString();
    }
    return nullptr;
}

void DPStream::handleReq(yhchaos::DPReq::ptr req) {
    yhchaos::DPRsp::ptr rsp = req->createRsp();
    if(!m_requestHandler(req, rsp
        ,std::dynamic_pointer_cast<DPStream>(shared_from_this()))) {
        sendMSG(rsp);
        //innerClose();
        close();
    } else {
        sendMSG(rsp);
    }
}

void DPStream::handleNotify(yhchaos::DPNotify::ptr nty) {
    if(!m_notifyHandler(nty
        ,std::dynamic_pointer_cast<DPStream>(shared_from_this()))) {
        //innerClose();
        close();
    }
}

DPSession:: DPSession(Sock::ptr sock)
    :DPStream(sock) {
    m_autoConnect = false;
}

DPClient::DPClient()
    :DPStream(nullptr) {
    m_autoConnect = true;
}

bool DPClient::connect(yhchaos::NetworkAddress::ptr addr) {
    m_socket = yhchaos::Sock::CreateTCP(addr);
    return m_socket->connect(addr);
}

DPSDLoadBalance::DPSDLoadBalance(ISD::ptr sd)
    :SDLoadBalance(sd) {
}

static SockStream::ptr create_dp_stream(ServiceItemInfo::ptr info) {
    yhchaos::IPNetworkAddress::ptr addr = yhchaos::NetworkAddress::SearchForAnyIPNetworkAddress(info->getIp());
    if(!addr) {
        YHCHAOS_LOG_ERROR(g_logger) << "invalid service info: " << info->toString();
        return nullptr;
    }
    addr->setPort(info->getPort());

    DPClient::ptr conn(new DPClient);

    yhchaos::CoSchedulerMgr::GetInstance()->coschedule("service_io", [conn, addr](){
        conn->connect(addr);
        conn->start();
    });
    return conn;
}

void DPSDLoadBalance::start() {
    m_cb = create_dp_stream;
    initConf(g_dp_services->getValue());
    SDLoadBalance::start();
}

void DPSDLoadBalance::start(const std::unordered_map<std::string
                              ,std::unordered_map<std::string,std::string> >& confs) {
    m_cb = create_dp_stream;
    initConf(confs);
    SDLoadBalance::start();
}

void DPSDLoadBalance::stop() {
    SDLoadBalance::stop();
}

DPRes::ptr DPSDLoadBalance::request(const std::string& domain, const std::string& service,
                                           DPReq::ptr req, uint32_t timeout_ms, uint64_t idx) {
    auto lb = get(domain, service);
    if(!lb) {
        return std::make_shared<DPRes>(ILoadBalance::NO_SERVICE, 0, nullptr, req);
    }
    auto conn = lb->get(idx);
    if(!conn) {
        return std::make_shared<DPRes>(ILoadBalance::NO_CONNECTION, 0, nullptr, req);
    }
    uint64_t ts = yhchaos::GetCurrentMS();
    auto& stats = conn->get(ts / 1000);
    stats.incDoing(1);
    stats.incTotal(1);
    auto r = conn->getStreamAs<DPStream>()->request(req, timeout_ms);
    uint64_t ts2 = yhchaos::GetCurrentMS();
    if(r->result == 0) {
        stats.incOks(1);
        stats.incUsedTime(ts2 -ts);
    } else if(r->result == AsyncSockStream::TIMEOUT) {
        stats.incTimeouts(1);
    } else if(r->result < 0) {
        stats.incErrs(1);
    }
    stats.decDoing(1);
    return r;
}

}
