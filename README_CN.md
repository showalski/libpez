# libpez
[![Build Status](https://img.shields.io/badge/README-English-yellow.svg)](README.md)

## 概述
libpez 是基于C语言的构建在ZMQ上的一个动态链接库（当然也可以很容易的做为一个模块集成到其它系统中），用它可以方便的进行线程间通信。它依赖于libev和libzmq两个动态链接库，消息的序列化使用google protobuf。如果你的程序构建在基于事件驱动的架构（libev，libevent）上可以使用libpez，libev相较于其它的事件驱动框架而言更加轻量级，较适用于嵌入式环境中。下面是一个简要的架构图：

<img src="https://github.com/showalski/pez/blob/master/pics/pez%20overview.png" width="660">

在libpez中，有一个专门的线程router thread，专注于线程间的消息转发。该线程是一个隐藏线程，在libpze初始化时创建（`pez_ipc_init`）。

libpez依赖于libzmq进行线程间通信，在内部中会使用libzmq中特定的sockets：ZMQ_DEALER。在初始化的过程中，每个线程需注册特定的一个字符串，之后该字符串与ZMQ_DEALER关联。router thread会创建ZMQ_ROUTER socket并绑定到一个特定的地址（类似与"IP地址:端口号"，这里绑定的地址主要用于进程内部使用，进程外不可见），每个注册的线程会与自动与ZMQ_DEALER连接（调用`pez_ipc_thread_init_rx`）。这些细节被隐藏到libpez中，应用程序只需关注本身的事情即可。下面是一个消息流的示意图：

<img src="https://github.com/showalski/pez/blob/master/pics/pez%20internal%20zmq%20sockets.png" width="480">

## 如何调试
libpez提供一个内部调试API可打印出调试信息，每个注册的线程在其内部都有两个计数器，用以统计已收发多少个消息；router thread拥有自己的计数器，可用于统计进程内部的消息收发个数。除此之外消息可以以类似hexdump的格式打印出来。
```
0000: 08001203666F6F1A 046D61696E320F08 ....foo..main2..   | pez msg snd(foo)
0010: 00120B7468697320 697320666F6F     ...this is foo     | pez msg snd(foo)
foo: snd cnt: 3
0000: 666F6F                            foo                | rt(src id)
0000: 6D61696E                          main               | rt(trgt id)
0000: 08001203666F6F1A 046D61696E320F08 ....foo..main2..   | rt(data)
0010: 00120B7468697320 697320666F6F     ...this is foo     | rt(data)
rt counter:main: recv:5, send:6
rt counter:bar: recv:1, send:0
rt counter:foo: recv:1, send:0
0000: 08001203666F6F1A 046D61696E320F08 ....foo..main2..   | pez msg recv(main)
0010: 00120B7468697320 697320666F6F     ...this is foo     | pez msg recv(main)
```

在上面的调试信息中，`|` 之后的信息表示该信息属于那个线程，否则多线程环境下消息会参杂一起，无法分辨。

## 其它说明
libpez本身严重依赖libzmq和libev，还有其它很多要优化的问题。由事件驱动的框架通常分为两种：电平触发；边缘触发，嵌入zmq到libev中是比较困难的，因为zmq的socket是边缘触发，而libev是电平触发，所幸的是libev提供了而外的API可以对两者进行融合，下面是一些好的文章以供参考：
1. [关于libzmq和libev两者事件触发机制的对比](https://funcptr.net/2012/09/10/zeromq---edge-triggered-notification/)
2. [如何将zmq嵌入到libev的事件循环中](https://funcptr.net/2013/04/20/embedding-zeromq-in-the-libev-event-loop/)
3. [参考代码](https://github.com/pijyoi/zmq_libev)
