#ifndef PEZ_IPC_H
#define PEZ_IPC_H
#include "ev_zsock.h"

typedef int    pez_status;

#define INPROC_ADDRESS          "inproc://channel"

#define INPROC_MAX_MSG_SIZE     1024

#define EOK                     0

void pez_ipc_init();

pez_status pez_ipc_thread_init_tx(int thread_id);

pez_status pez_ipc_thread_init_rx(struct ev_loop *loop, int thread_id, ev_zsock_cbfn cb);

pez_status pez_ipc_msg_recv(void *socket, void *buf, size_t buffer_size, size_t *rtn_size);

pez_status pez_ipc_msg_send (int trgt, int src, void *buf, size_t size);

pez_status pez_ipc_context_init(struct ev_loop *loop, int id, void *zmq_ctx, ev_zsock_cbfn cb);

#endif /* PEZ_IPC_H */
