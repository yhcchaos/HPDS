#ifndef __YHCHAOS_DP_DP_STREAM_H__
#define __YHCHAOS_DP_DP_STREAM_H__

#include "yhchaos/streams/async_sock_stream.h"
#include "dp_message.h"
#include "yhchaos/streams/loadbalance.h"
#include <boost/any.hpp>

namespace yhchaos {

struct DPRes {
   typedef std::shared_ptr<DPRes> ptr; 
   DPRes(int32_t _res, int32_t _used, DPRsp::ptr rsp, DPReq::ptr req)
    :result(_res)
    ,used(_used)
    ,response(rsp)
    ,request(req) {
   }
   int32_t result;
   //从发送到接收到回复用了多长时间
   int32_t used;
   DPRsp::ptr response;
   DPReq::ptr request;

   std::string toString() const;
};

class DPStream : public yhchaos::AsyncSockStream {
public:
    typedef std::shared_ptr<DPStream> ptr;
    typedef std::function<bool(yhchaos::DPReq::ptr
                               ,yhchaos::DPRsp::ptr
                               ,yhchaos::DPStream::ptr)> request_handler;
    typedef std::function<bool(yhchaos::DPNotify::ptr
                               ,yhchaos::DPStream::ptr)> notify_handler;

    DPStream(Sock::ptr sock);
    ~DPStream();

    request_handler getReqHandler() const { return m_requestHandler;}
    notify_handler getNotifyHandler() const { return m_notifyHandler;}

    void setReqHandler(request_handler v) { m_requestHandler = v;}
    void setNotifyHandler(notify_handler v) { m_notifyHandler = v;}

    /**
     * @brief 发送消息
     * @param[in] msg 消息
     * @details 将消息封装成DPSendCtx，然后入队列m_queue，从而唤醒do_write协程发送消息
    */
    int32_t sendMSG(MSG::ptr msg);

    DPRes::ptr request(DPReq::ptr req, uint32_t timeout_ms);

    template<class T>
    void setData(const T& v) {
        m_data = v;
    }

    template<class T>
    T getData() {
        try {
            return boost::any_cast<T>(m_data);
        } catch(...) {
        }
        return T();
    }
protected:
    struct DPSendCtx : public SendCtx {
        typedef std::shared_ptr<DPSendCtx> ptr;
        MSG::ptr msg;
        /**
         * @brief 发送Req
         * @param[in] stream DPStream, 用于发送Req
         * @details 添加dp头，然后转换为DPReq，然后发送
        */
        virtual bool doSend(AsyncSockStream::ptr stream) override;
    };

    struct DPCtx : public Ctx {
        typedef std::shared_ptr<DPCtx> ptr;
        DPReq::ptr request;
        DPRsp::ptr response;
        //发送request
        virtual bool doSend(AsyncSockStream::ptr stream) override;
    };

    /**
     * @details
     *  1. 从stream中读出DPRsp/DPReq/DPNotify，继承自Rsp/Req/Notify
     *  2. 如果是DPRsp，直接从m_ctxs中根据DPRsp的sn取出并删除存储在m_ctxs中的DPCtx，设置result,sn,reponse并返回
     *  3. 如果是DPReq，就将handleReq放到m_workers中进行调度，返回nullptr
     *  4. 如果是DPNotify，就将handleNotify放到m_workers中进行调度，返回nullptr
    */
    virtual Ctx::ptr doRecv() override;

    /**
     * @brief 当收到request时，发送相应的reponse
     * @param[in] req 收到的request
     * @details 
     *  1. 根据request在m_requestHandler中创建相应的response
     *  2. 通过调用sendMSG将reponse入队列m_queue，从而唤醒do_write函数发送response
    */
    void handleReq(yhchaos::DPReq::ptr req);
    void handleNotify(yhchaos::DPNotify::ptr nty);
private:
    DPMSGDecoder::ptr m_decoder;//new DPMSGDecoder
    //这个函数会判断生成response并发送后是否需要关闭连接，如果返回false，就关闭连接
    //param : DPReq, DPRsp, DPStream. return bool
    request_handler m_requestHandler;
    //param : DPNotify, DPStream. return bool
    notify_handler m_notifyHandler;
    boost::any m_data;
};

class DPSession : public DPStream {
public:
    typedef std::shared_ptr<DPSession> ptr;
    DPSession(Sock::ptr sock);
};

class DPConnection : public DPStream {
public:
    typedef std::shared_ptr<DPConnection> ptr;
    DPConnection();
    bool connect(yhchaos::NetworkAddress::ptr addr);
};

class DPSDLoadBalance : public SDLB {
public:
    typedef std::shared_ptr<DPSDLoadBalance> ptr;
    DPSDLoadBalance(ISD::ptr sd);

    virtual void start();
    virtual void stop();
    void start(const std::unordered_map<std::string
               ,std::unordered_map<std::string,std::string> >& confs);

    DPRes::ptr request(const std::string& domain, const std::string& service,
                             DPReq::ptr req, uint32_t timeout_ms, uint64_t idx = -1);

};

}

#endif
