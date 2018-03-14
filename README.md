# pez(TBD)

PEZ is shared library which can be used to simpify communications between threads. It depends on libev and libzmq. Below is an overview figure

![image of whole picture](https://github.com/showalski/pez/blob/master/pics/pez%20overview.png)

In pez, there is a specific thread, router thread, dedicating to route messages between threads. However router thread cannot be seen by application threads because it's created by pez library. 

Regarding to ZMQ sockets:During initialization, each thread needs to tell its ID(normally a integer) to pez library. This ID would be set to the identity of ZMQ_DEALER socket. router thread would create ZMQ_ROUTER socket and bind it to a in-process address. Other threads would connect ZMQ_ROUTER socket by calling pez API. Those details are hiden in pez library, so the application can focus on other tasks.
