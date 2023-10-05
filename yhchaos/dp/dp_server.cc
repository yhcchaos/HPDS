#include "dp_server.h"
#include "yhchaos/log.h"
#include "yhchaos/modularity.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");

DPSvr::DPSvr(const std::string& type
                       ,yhchaos::IOCoScheduler* worker
                       ,yhchaos::IOCoScheduler* io_worker
                       ,yhchaos::IOCoScheduler* accept_worker)
    :TcpSvr(worker, io_worker, accept_worker) {
    m_type = type;
}

void DPSvr::handleClient(Sock::ptr client) {
    YHCHAOS_LOG_DEBUG(g_logger) << "handleClient " << *client;
    yhchaos::DPSession::ptr session(new yhchaos::DPSession(client));
    session->setWorker(m_worker);
    ModularityMgr::GetInstance()->foreach(Modularity::DP,
            [session](Modularity::ptr m) {
        m->onConnect(session);
    });
    
    session->setDisconnectCb(
        [](AsyncSockStream::ptr stream) {
            //执行所有modules的disconnect函数
             ModularityMgr::GetInstance()->foreach(Modularity::DP,
                    [stream](Modularity::ptr m) {
                m->onDisconnect(stream);
            });
        }
    );
    //多个模块根据request去共同生成一个response，每个模块负责response的一个部分
    session->setReqHandler(
        [](yhchaos::DPReq::ptr req
           ,yhchaos::DPRsp::ptr rsp
           ,yhchaos::DPStream::ptr conn)->bool {
            //YHCHAOS_LOG_INFO(g_logger) << "handleReq " << req->toString()
            //                         << " body=" << req->getBody();
            bool rt = false;
            //对req，rsp，conn执行所有modules的handleReq函数
            //======
            ModularityMgr::GetInstance()->foreach(Modularity::DP,
                    [&rt, req, rsp, conn](Modularity::ptr m) {
                if(rt) {
                    return;
                }
                rt = m->handleReq(req, rsp, conn);
            });
            //======
            return rt;
        }
    ); 
    session->setNotifyHandler(
        [](yhchaos::DPNotify::ptr nty
           ,yhchaos::DPStream::ptr conn)->bool {
            YHCHAOS_LOG_INFO(g_logger) << "handleNty " << nty->toString()
                                     << " body=" << nty->getBody();
            bool rt = false;
            ////执行所有modules的handleNotify函数
            ModularityMgr::GetInstance()->foreach(Modularity::DP,
                    [&rt, nty, conn](Modularity::ptr m) {
                if(rt) {
                    return;
                }
                rt = m->handleNotify(nty, conn);
            });
            return rt;
        }
    );
    session->start();
}

}
