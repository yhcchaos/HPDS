#include <iostream>
#include "yhchaos/log.h"
#include "yhchaos/util.h"

int main(int argc, char** argv) {
    yhchaos::Logger::ptr logger(new yhchaos::Logger);
    logger->addAppender(yhchaos::LogAppender::ptr(new yhchaos::StdoutLogAppender));

    yhchaos::FileLogAppender::ptr file_appender(new yhchaos::FileLogAppender("./log.txt"));
    yhchaos::LogFormatter::ptr fmt(new yhchaos::LogFormatter("%d%T%p%T%m%n"));
    file_appender->setFormatter(fmt);
    file_appender->setLevel(yhchaos::LogLevel::ERROR);

    logger->addAppender(file_appender);

    //yhchaos::LogFdEvent::ptr event(new yhchaos::LogFdEvent(__FILE__, __LINE__, 0, yhchaos::GetCppThreadId(), yhchaos::GetCoroutineId(), time(0)));
    //event->getSS() << "hello yhchaos log";
    //logger->log(yhchaos::LogLevel::DEBUG, event);
    std::cout << "hello yhchaos log" << std::endl;

    YHCHAOS_LOG_INFO(logger) << "test macro";
    YHCHAOS_LOG_ERROR(logger) << "test macro error";

    YHCHAOS_LOG_FMT_ERROR(logger, "test macro fmt error %s", "aa");

    auto l = yhchaos::LoggerMgr::GetInstance()->getLogger("xx");
    YHCHAOS_LOG_INFO(l) << "xxx";
    return 0;
}
