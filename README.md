# pez(TBD)

PEZ is shared library which can be used to simpify communications between threads. It depends on libev and libzmq. Below is an overview figure

<img src="https://github.com/showalski/pez/blob/master/pics/pez%20overview.png" width="660">

In pez, there is a specific thread, router thread, dedicating to route messages between threads. However router thread cannot be seen by application threads because it's created by pez API:`pez_ipc_init`. 

Regarding to ZMQ sockets:During initialization, each thread needs to tell its ID(normally an integer) to pez library. This ID would be set to the identity of ZMQ_DEALER socket. Router thread would create ZMQ_ROUTER socket and bind itself to an in-process address. Other threads would connect ZMQ_ROUTER socket during intialization by calling pez API`pez_ipc_thread_init_rx`. Those details are hiden in pez library, so the application can focus on other tasks. Below figure shows the messages flow and internal zmq sockets orgnization

<img src="https://github.com/showalski/pez/blob/master/pics/pez%20internal%20zmq%20sockets.png" align="left" width="480">
