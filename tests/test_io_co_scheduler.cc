#include "yhchaos/yhchaos.h"
#include "yhchaos/iocoscheduler.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

int sock = 0;

void test_coroutine() {
    YHCHAOS_LOG_INFO(g_logger) << "test_coroutine sock=" << sock;

    //sleep(3);

    //close(sock);
    //yhchaos::IOCoScheduler::GetThis()->cancelAll(sock);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "115.239.210.27", &addr.sin_addr.s_addr);

    if(!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {
    } else if(errno == EINPROGRESS) {
        YHCHAOS_LOG_INFO(g_logger) << "add event errno=" << errno << " " << strerror(errno);
        yhchaos::IOCoScheduler::GetThis()->addFdEvent(sock, yhchaos::IOCoScheduler::READ, [](){
            YHCHAOS_LOG_INFO(g_logger) << "read callback";
        });
        yhchaos::IOCoScheduler::GetThis()->addFdEvent(sock, yhchaos::IOCoScheduler::WRITE, [](){
            YHCHAOS_LOG_INFO(g_logger) << "write callback";
            //close(sock);
            yhchaos::IOCoScheduler::GetThis()->cancelFdEvent(sock, yhchaos::IOCoScheduler::READ);
            close(sock);
        });
    } else {
        YHCHAOS_LOG_INFO(g_logger) << "else " << errno << " " << strerror(errno);
    }

}

void test1() {
    std::cout << "EPOLLIN=" << EPOLLIN
              << " EPOLLOUT=" << EPOLLOUT << std::endl;
    yhchaos::IOCoScheduler iom(2, false);
    iom.coschedule(&test_coroutine);
}

yhchaos::TimedCoroutine::ptr s_timer;
void test_timer() {
    yhchaos::IOCoScheduler iom(2);
    s_timer = iom.addTimedCoroutine(1000, [](){
        static int i = 0;
        YHCHAOS_LOG_INFO(g_logger) << "hello timer i=" << i;
        if(++i == 3) {
            s_timer->reset(2000, true);
            //s_timer->cancel();
        }
    }, true);
}

int main(int argc, char** argv) {
    //test1();
    test_timer();
    return 0;
}
