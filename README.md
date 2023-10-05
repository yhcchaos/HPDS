# HPDS
c++高性能分布式服务器
## 1. 日志模块

日志模块，支持流式和格式化两种日志写入风格，还提供了日志格式、日志级别、多通道日志等自定义功能。

流式日志的使用方式如下：

```cpp
YHCHAOS_LOG_INFO(g_logger) << "使用流式日志";
```

格式化日志的使用方式如下：

```cpp
YHCHAOS_LOG_FMT_INFO(g_logger, "%s", "使用格式化日志");
```

该日志模块允许灵活配置日志输出内容，包括时间、线程ID、线程名称、日志级别、日志名称、文件名和行号等信息。

## 2. 配置模块

允许用户定义配置项而无需单独解析配置文件。配置使用YAML文件格式，并支持各种数据类型、STL容器（如vector、list、set、map）以及自定义类型的配置。

配置项的定义方式如下：

```cpp
static yhchaos::AppConfigVar<int>::ptr g_tcp_connect_timeout =
    yhchaos::AppConfig::Lookup("tcp.connect.timeout", 5000, "TCP连接超时时间");
```

配置文件示例（YAML格式）：

```yaml
tcp:
    connect:
        timeout: 10000
```

当配置文件修改并重新加载时，配置项的值会自动更新。

## 3. 线程模块

线程模块封装了pthread库中的一些常用功能，包括Thread、Semaphore、Mutex、RWMutex、Spinlock等对象，使线程操作更加便捷。不采用C++11线程的原因在于，C++11线程本质上也是基于pthread实现的。此外，C++11标准库中未提供读写互斥锁（RWMutex）和自旋锁（Spinlock）等在高并发场景中常用的功能，因此选择了自行封装pthread。

## 4. 协程模块

引入协程的概念，协程是一种轻量级的用户态线程，相当于线程中的线程。它可以将复杂的异步调用封装成同步操作，降低业务逻辑的复杂性。当前版本的协程模块基于ucontext_t实现，未来计划支持采用boost.context的方式实现。

## 5. 协程调度模块

协程调度器是核心组件，用于管理协程的调度。它内部使用线程池实现，支持协程在多线程中切换，也可以指定协程在特定线程中执行。这种调度模型是一种N-M的协程调度模型，其中N个线程管理M个协程，以最大程度地充分利用每个线程的执行能力。

## 6. IO协程调度模块

IO协程调度模块是协程调度器的扩展，它封装了epoll（在Linux下），并支持定时器功能（使用epoll实现，精度为毫秒级）。此模块支持添加、删除和取消Socket读写事件，还支持一次性定时器、循环定时器和条件定时器等功能。

## 7. Hook模块

Hook模块用于拦截和修改系统底层的socket相关API、socket IO相关API以及sleep系列的API。Hook的开启控制是线程级别的，用户可以自由选择。通过Hook模块，一些不具备异步功能的API可以表现出异步性能，例如MySQL等。

## 8. Socket模块

封装了Socket类，提供了所有socket API的功能。它还统一封装了地址类，包括IPv4、IPv6和Unix地址，并提供了域名解析和IP解析的功能。

## 9. ByteArray序列化模块

ByteArray序列化模块提供了对二进制数据的常用操作，包括基本数据类型（int8_t、int16_t、int32_t、int64_t等）的读写、Varint和std::string的支持、字节序转换、序列化到文件以及从文件反序列化等功能。

## 10. TcpServer模块

TcpServer模块基于Socket类封装了通用的TcpServer服务器类，提供了简单的API，可快速绑定一个或多个地址、启动服务、监听端口、接受连接以及处理Socket连接等功能。用户只需继承该类并实现特定业务功能即可快速创建服务器。

## 11. Stream模块

Stream模块封装了统一的流式接口，使文件和Socket等资源可以以统一的风格操作。这种统一的风格可以提供更灵活的扩展。目前，已实现了SocketStream。

## 12. HTTP模块

采用Ragel有限状态机实现了HTTP/1.1协议和URI解析。该模块基于SocketStream实现了HttpConnection（HTTP客户端）和HttpSession（HTTP服务器端连接）。此外，基于TcpServer实现了HttpServer，提供了完整的HTTP客户端API请求和HTTP服务器功能。

## 13. Servlet模块

Servlet模块仿照Java的Servlet，实现了一套Servlet接口，包括ServletDispatch、FunctionServlet和NotFoundServlet等。它支持URI的精确匹配和模糊匹配等功能，与HTTP模块一起提供了完整的HTTP服务器功能。
