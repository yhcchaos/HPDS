#ifndef __FILE_MANAGER_H__
#define __FILE_MANAGER_H__

#include <memory>
#include <vector>
#include "cpp_thread.h"
#include "singleton.h"

namespace yhchaos {

/**
 * @brief 文件句柄上下文类
 * @details 管理文件句柄类型(是否socket)
 *          是否阻塞,是否关闭,读/写超时时间
 */
class FileContext : public std::enable_shared_from_this<FileContext> {
public:
    typedef std::shared_ptr<FileContext> ptr;
    /**
     * @brief 通过文件句柄构造FileContext
     */
    FileContext(int fd);
    /**
     * @brief 析构函数
     */
    ~FileContext();

    /**
     * @brief 是否初始化完成
     */
    bool isInit() const { return m_isInit;}

    /**
     * @brief 是否socket
     */
    bool isSock() const { return m_isSock;}

    /**
     * @brief 是否已关闭
     */
    bool isClose() const { return m_isClosed;}

    /**
     * @brief 设置用户主动设置非阻塞
     * @param[in] v 是否阻塞
     */
    void setUserNonblock(bool v) { m_userNonblock = v;}

    /**
     * @brief 获取是否用户主动设置的非阻塞
     */
    bool getUserNonblock() const { return m_userNonblock;}

    /**
     * @brief 设置系统非阻塞
     * @param[in] v 是否阻塞
     */
    void setSysNonblock(bool v) { m_sysNonblock = v;}

    /**
     * @brief 获取系统非阻塞
     */
    bool getSysNonblock() const { return m_sysNonblock;}

    /**
     * @brief 设置超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @param[in] v 时间毫秒
     */
    void setTimeout(int type, uint64_t v);

    /**
     * @brief 获取超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @return 超时时间毫秒
     */
    uint64_t getTimeout(int type);
private:
    /**
     * @brief 初始化
     */
    bool init();
private:
    /// 是否初始化
    bool m_isInit: 1;
    /// 是否socket
    bool m_isSock: 1;
    /// 是否hook非阻塞，是否是系统设置的Nonblock，如果是socket则总是true，如果是其他类型false
    bool m_sysNonblock: 1;
    /// 是否用户主动设置非阻塞，
    bool m_userNonblock: 1;
    /// 是否关闭
    bool m_isClosed: 1;
    /// 文件句柄
    int m_fd;
    /// 读超时时间毫秒
    uint64_t m_recvTimeout;
    /// 写超时时间毫秒
    uint64_t m_sendTimeout;
};

/**
 * @brief 文件句柄管理类
 */
class FileManager {
public:
    typedef RWMtx RWMtxType;
    /**
     * @brief 无参构造函数
     */
    FileManager();

    /**
     * @brief 获取/创建文件句柄类FileContext
     * @param[in] fd 文件句柄
     * @param[in] auto_create 是否自动创建
     * @return 返回对应文件句柄类FileContext::ptr
     */
    FileContext::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除文件句柄类
     * @param[in] fd 文件句柄
     */
    void del(int fd);
private:
    /// 读写锁
    RWMtxType m_mutex;
    /// 文件句柄集合
    //FileContext中的fd和m_datas的下标是一一对应的,fd=i的FileContext就是m_datas[i]
    std::vector<FileContext::ptr> m_datas;
};

/// 文件句柄单例
typedef Singleton<FileManager> FdMgr;

}

#endif
