#ifndef PEZ_IPC_H
#define PEZ_IPC_H
#include "pez_common.h"
#include "ev_zsock.h"
#include "msg.pb-c.h"
 
typedef int    pez_status;
 
#define INPROC_ADDRESS          "inproc://channel"
 
#define INPROC_MAX_MSG_SIZE     1024
 
void pez_ipc_init();
 
pez_status pez_ipc_thread_init(struct ev_loop *loop, int thread_id, ev_zsock_cbfn cb);
 
pez_status pez_ipc_msg_recv(void *socket, void *buf, int buffer_size, int *rtn_size);
 
pez_status pez_ipc_msg_send (int trgt, int src, void *buf, int size);
 
#endif /* PEZ_IPC_H */
