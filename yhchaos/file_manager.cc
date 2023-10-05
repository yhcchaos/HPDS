#include "file_manager.h"
#include "hookfunc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace yhchaos {

FileContext::FileContext(int fd)
    :m_isInit(false)
    ,m_isSock(false)
    ,m_sysNonblock(false)
    ,m_userNonblock(false)
    ,m_isClosed(false)
    ,m_fd(fd)
    ,m_recvTimeout(-1)
    ,m_sendTimeout(-1) {
    init();
}

FileContext::~FileContext() {
}

bool FileContext::init() {
    if(m_isInit) {
        return true;
    }
    m_recvTimeout = -1;
    m_sendTimeout = -1;

    struct stat fd_stat;
    //确实是打开的文件描述符
    if(-1 == fstat(m_fd, &fd_stat)) {
        m_isInit = false;
        m_isSock = false;
    } else {
        m_isInit = true;
        m_isSock = S_ISSOCK(fd_stat.st_mode);
    }
    //如果是socket，并且没有设置为非阻塞，那么就设置为非阻塞,
    //m_sysNonblock总为true（如果事先设置了阻塞了，也为true）
    //也就是说只要这个fd是socket，那么m_sysNonblock就是true
    if(m_isSock) {
        int flags = fcntl_fun(m_fd, F_GETFL, 0);
        if(!(flags & O_NONBLOCK)) {
            fcntl_fun(m_fd, F_SETFL, flags | O_NONBLOCK);
        }
        m_sysNonblock = true;
    } else {//如果不是socket，m_sysNonblock就是false
        m_sysNonblock = false;
    }

    m_userNonblock = false;
    m_isClosed = false;
    return m_isInit;
}

void FileContext::setTimeout(int type, uint64_t v) {
    if(type == SO_RCVTIMEO) {
        m_recvTimeout = v;
    } else {
        m_sendTimeout = v;
    }
}

uint64_t FileContext::getTimeout(int type) {
    if(type == SO_RCVTIMEO) {
        return m_recvTimeout;
    } else {
        return m_sendTimeout;
    }
}

FileManager::FileManager() {
    m_datas.resize(64);
}

FileContext::ptr FileManager::get(int fd, bool auto_create) {
    if(fd == -1) {
        return nullptr;
    }
    RWMtxType::ReadLock lock(m_mutex);
    if((int)m_datas.size() <= fd) {
        if(auto_create == false) {
            return nullptr;
        }
    } else {
        if(m_datas[fd] || !auto_create) {
            return m_datas[fd];
        }
    }
    lock.unlock();

    RWMtxType::WriteLock lock2(m_mutex);
    FileContext::ptr ctx(new FileContext(fd));
    if(fd >= (int)m_datas.size()) {
        m_datas.resize(fd * 1.5);
    }
    m_datas[fd] = ctx;
    return ctx;
}
//m_datas的大小不改变，只是将对应的fd的FileContext置空
void FileManager::del(int fd) {
    RWMtxType::WriteLock lock(m_mutex);
    if((int)m_datas.size() <= fd) {
        return;
    }
    m_datas[fd].reset();
}

}
