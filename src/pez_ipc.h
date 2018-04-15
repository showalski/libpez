#ifndef PEZ_IPC_H
#define PEZ_IPC_H
#include "ev_zsock.h"

typedef int    pez_status;

#define INPROC_ADDRESS          "inproc://channel"

#define INPROC_MAX_MSG_SIZE     1024

#define EOK                     0

void pez_ipc_init();

pez_status pez_ipc_thread_init_tx(const char *tx_id);

pez_status pez_ipc_thread_init_rx(struct ev_loop *loop, const char *recv_id, ev_zsock_cbfn cb);

pez_status pez_ipc_msg_recv(void *socket, void *buf, size_t buffer_size, size_t *rtn_size);

pez_status pez_ipc_msg_send (const char *trgt, const char *src, void *buf, size_t size);

void pez_ipc_router_counter_print();
#endif /* PEZ_IPC_H */
