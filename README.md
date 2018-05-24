# pez(TBD)
## Overview
PEZ is shared library and abstract layer above zmq which facilitates communications between C threads. It depends on libev and libzmq. **Message receiving is treated as libev events.** Below is an overview figure:

<img src="https://github.com/showalski/pez/blob/master/pics/pez%20overview.png" width="660">

In pez, there is a specific thread, router thread, dedicating to route messages between threads. However router thread cannot be seen by application threads because it's created by pez API:`pez_ipc_init`. 

Regarding to ZMQ sockets:During initialization, each thread needs to tell its ID(normally an integer) to pez library. This ID would be set to the identity of ZMQ_DEALER socket. Router thread would create ZMQ_ROUTER socket and bind itself to an in-process address. Other threads would connect ZMQ_ROUTER socket during intialization by calling pez API`pez_ipc_thread_init_rx`. Those details are hiden in pez library, so the application can focus on other tasks. Below figure shows the messages flow and internal zmq sockets orgnization

<img src="https://github.com/showalski/pez/blob/master/pics/pez%20internal%20zmq%20sockets.png" width="480">

## How to debug
One API enables internal debug switch to print detailed info to console. Each registered thread has internal counters telling how many messages it received/sent. Besides threads' counters, the router thread has its counters revealing overall counters in libev. Except for counters raw message dumping is available.
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
Strings after `|` during dumping zmq mesages are suffixes for differentiating which thread is printing, which could help debugging during multi-thread env.

## Explanation
Embedding zmq to libev is a bit of tricky because everthing is event in libev and we don't want waste time waiting/polling zmq messages, othewise no events would be received. We know that there are 2 different interrupts in circuit: level or edge triggered. The difficulties are that `zmq sockets are edge-triggered` but `libev is level-triggered`. Fortunately libev provides some APIs to achieve our goals. There are some good articles/links with detailed explaination:

1. [Differences between the trigger fasions of libev and zmq](https://funcptr.net/2012/09/10/zeromq---edge-triggered-notification/)
2. [How to embed zmq to libev event loop](https://funcptr.net/2013/04/20/embedding-zeromq-in-the-libev-event-loop/)
3. [Implementation of above embedding](https://github.com/pijyoi/zmq_libev)
