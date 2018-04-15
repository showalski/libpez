#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zmq.h>
#include <pthread.h>
#include "pez_ipc.h"
#include "ev_zsock.h"
#include <assert.h>
#ifdef __APPLE__
#include <mach/error.h>
#else
#include <error.h>
#endif

#define PEZ_SOCK_MAX_NUM        (1024)
#define PEZ_SOCK_ID_MAX_LEN     (32)
#define PEZ_SOCK_ID_INVAL       (-1)

typedef struct {
    pthread_t           pid;
    struct ev_zsock_t   pez_ev_zsock;
    char                identity[PEZ_SOCK_ID_MAX_LEN];
    uint64_t            recv_cnt;
    uint64_t            send_cnt;
} pez_thd_t;

typedef struct {
    void                *zmq_ctx;
    pthread_t           tid_router;
    unsigned int        thread_num;
    pthread_mutex_t     lock;
    pez_thd_t           thd[PEZ_SOCK_MAX_NUM];
} pez_t;

static pez_t pez;

/*
 * Allocate numerical value for specific string. If no room for that string
 * PEZ_SOCK_ID_INVAL will be returned.
 */
static void
pez_ipc_id_alloc(const char *str, int32_t *id) {
    int32_t     i = 0;
    pthread_mutex_lock(&pez.lock);
    for ( ; i < PEZ_SOCK_MAX_NUM; i ++) {
        if (strnlen(pez.thd[i].identity, PEZ_SOCK_ID_MAX_LEN) == 0) {
            strncpy(pez.thd[i].identity, str, PEZ_SOCK_ID_MAX_LEN);
            *id = i;
            //printf("pez ipc: alloc id %d for %s\n", i, str);
            break;
        }
    }
    if (i == PEZ_SOCK_MAX_NUM) {
        *id = PEZ_SOCK_ID_INVAL;
    }
    pthread_mutex_unlock(&pez.lock);
}

/*
 * Find numerical id for given str. If it isn't in there -1 will be returned.
 */
static void
pez_ipc_id_get(const char *str, int32_t *id) {
    int32_t i = 0;

    for ( ; i < PEZ_SOCK_MAX_NUM; i ++) {
        if (!strncmp(pez.thd[i].identity, str, PEZ_SOCK_ID_MAX_LEN)) {
            //printf("pez ipc: id %d <=> str %s\n", i, str);
            *id = i;
            break;
        }
    }
    if (i == PEZ_SOCK_MAX_NUM) {
        *id = PEZ_SOCK_ID_INVAL;
    }
}

/*
 * Get zmq context.
 * One zmq context is enough and a must:
 *      1. You should create and use exactly one context in your process.
 *      2. One zmq context allows per gigabyte of data in or out per second.
 */
static void *
pez_ipc_get_zmq_ctx() {
    pthread_mutex_lock(&pez.lock);
    if (pez.zmq_ctx == NULL) {
        pez.zmq_ctx = zmq_ctx_new();
        assert (pez.zmq_ctx != NULL);
    }
    pthread_mutex_unlock(&pez.lock);
    return pez.zmq_ctx;
}

/*
 * Send msg to router thread. router thread will route it.
 * TODO: Broadcasting message should be added.
 */
pez_status
pez_ipc_msg_send (const char *trgt, const char *src, void *buf, size_t size) {
    pez_status  rtn;
    int32_t     trgt_id, src_id;

    if(!buf || !trgt || !src) {
        return EINVAL;
    }

    pez_ipc_id_get(trgt, &trgt_id);
    if (trgt_id == PEZ_SOCK_ID_INVAL) {
        printf("pez ipc: invalid trgt thread name(%s)\n", trgt);
        return EINVAL;
    }
    pez_ipc_id_get(src, &src_id);
    if (src_id == PEZ_SOCK_ID_INVAL) {
        printf("pez ipc: invalid src thread name(%s)\n", src);
        return EINVAL;
    }

    if (pez.thd[src_id].pid != pthread_self()) {
        printf("pez ipc:src is incorrect\n");
        return EINVAL;
    }

    /* 1st: send target id frame */
    rtn = zmq_send(pez.thd[src_id].pez_ev_zsock.zsock,
                   trgt,
                   strnlen(trgt,PEZ_SOCK_ID_MAX_LEN),
                   ZMQ_SNDMORE);
    if (rtn == -1) {
        printf("pez ipc:send trgt id frame failed: %s\n", strerror(errno));
        return rtn;
    }

    /* 2nd: send data frame */
    rtn = zmq_send(pez.thd[src_id].pez_ev_zsock.zsock,
                   buf,
                   size,
                   0);
    if (rtn != size) {
        printf("pez ipc: msg send failed(sent %d bytes)\n", rtn);
        return rtn;
    }

    return EOK;
}

/*
 * recv message
 */
pez_status
pez_ipc_msg_recv(void *socket,
                 void *buf,
                 size_t buffer_size,
                 size_t *rtn_size) {
    pez_status rc;

    if (!socket || !buf || !rtn_size || (buffer_size == 0)) {
        printf("invalid params recvd\n");
        return EINVAL;
    }

    rc = zmq_recv(socket, buf, buffer_size, 0);
    if (rc == -1) {
        printf("%s: err:%s\n", __func__, strerror(errno));
    } else if (rc == 0) {
        printf("%s: recvd 0 byte msg\n", __func__);
    }

    *rtn_size = rc;

    return EOK;
}

/*
 * Didn't create zmq socket. Monitor only
 */
pez_status
pez_ipc_thread_init_tx(const char *tx_id) {
    void        *socket = NULL;
    pez_status  rc;
    void        *zmq_ctx = NULL;
    int32_t     id;

    if (!tx_id) {
        printf("pez ipc:invalid id recvd in tx creation\n");
        return EINVAL;
    }

    pez_ipc_id_get(tx_id, &id);
    if (id != PEZ_SOCK_ID_INVAL) {
        printf("pez ipc: don't invoke this API twice for same id. Previous"
               " call is by %s\n", pez.thd[id].identity);
        return EINVAL;
    }

    pez_ipc_id_alloc(tx_id, &id);
    if (id == PEZ_SOCK_ID_INVAL) {
        printf("pez ipc: no room for new tx thread(%s) allocation\n", tx_id);
        return ENOMEM;
    }

    pez.thd[id].pid = pthread_self();

    zmq_ctx = pez_ipc_get_zmq_ctx();
    if (!zmq_ctx) {
        printf("pez ipc: null zmq ctx recvd\n");
        return EINVAL;
    }

    socket = zmq_socket(zmq_ctx, ZMQ_DEALER);
    if (socket == NULL) {
        printf("unable to create ZMQ_DEALER socket: %s\n", strerror(errno));
        return errno;
    }

    /* set zmq id */
    rc = zmq_setsockopt (socket,
                         ZMQ_IDENTITY,
                         tx_id,
                         sizeof(int));
    if (rc == -1) {
        printf("unable to set ZMQ ID for thread %s:%s\n",
                    tx_id,
                    strerror(errno));
        return errno;
    }

    /* connect to router thread */
    rc = zmq_connect(socket, INPROC_ADDRESS);
    if (rc == -1) {
        printf("unable to connect router for thread %s:%s\n",
                    tx_id,
                    strerror(errno));
        return errno;
    }

    /* save socket to pez */
    pez.thd[id].pez_ev_zsock.zsock = socket;

    return EOK;
}


/*
 * Do zmq socket creation and connect.
 */
pez_status
pez_ipc_thread_init_rx(struct ev_loop *loop,
                       const char *rx_id,
                       ev_zsock_cbfn cb) {
    void        *socket = NULL;
    pez_status  rc;
    void        *zmq_ctx = NULL;
    int32_t     id;

    zmq_ctx = pez_ipc_get_zmq_ctx();

    if (!zmq_ctx || !cb || !loop || !rx_id) {
        printf("pez ipc:NULL zmq_ctx or input argus recvd\n");
        return EINVAL;
    }

    pez_ipc_id_get(rx_id, &id);
    if (id != PEZ_SOCK_ID_INVAL) {
        printf("pez ipc: don't invoke this API twice for same id. Previous"
               " call is by %s\n", pez.thd[id].identity);
        return EINVAL;
    }

    pez_ipc_id_alloc(rx_id, &id);
    if (id == PEZ_SOCK_ID_INVAL) {
        printf("pez ipc: no room for new rx thread(%s) allocation\n", rx_id);
        return ENOMEM;
    }

    pez.thd[id].pid = pthread_self();

    socket = zmq_socket(zmq_ctx, ZMQ_DEALER);
    if (!socket) {
        printf("unable to create ZMQ_DEALER socket for %s(%s)\n",
                rx_id,
                strerror(errno));
        return errno;
    }

    /* set zmq id */
    rc = zmq_setsockopt (socket,
                         ZMQ_IDENTITY,
                         rx_id,
                         strnlen(rx_id,PEZ_SOCK_ID_MAX_LEN));
    if (rc == -1) {
        printf("unable to set ZMQ ID for thread %s:%s\n",
                    rx_id,
                    strerror(errno));
        return errno;
    }

    /* connect to router thread */
    rc = zmq_connect(socket, INPROC_ADDRESS);
    if (rc == -1) {
        printf("unable to connect router for thread %s:%s\n",
                    rx_id,
                    strerror(errno));
        return errno;
    }

    /* Only need EV_READ event to read incoming msg */
    ev_zsock_init(&pez.thd[id].pez_ev_zsock, cb, socket, EV_READ);
    ev_zsock_start(loop, &pez.thd[id].pez_ev_zsock);

    return EOK;
}

/*
 *
 */
void
pez_ipc_router_counter_print() {
    int32_t     i;
    for (i = 0; i < PEZ_SOCK_MAX_NUM; i ++) {
        if (strnlen(pez.thd[i].identity, PEZ_SOCK_ID_MAX_LEN) != 0) {
            printf("%s: recv:%llu, send:%llu\n",
                    pez.thd[i].identity,
                    pez.thd[i].recv_cnt,
                    pez.thd[i].send_cnt);
        }
    }
}

/*
 *
 */
static void
pez_ipc_router_count(char *trgt, char *src) {
    int32_t     id = 0;;
    pez_ipc_id_get(trgt, &id);
    if (id != PEZ_SOCK_ID_INVAL) {
        pez.thd[id].recv_cnt ++;
    }
    pez_ipc_id_get(src, &id);
    if (id != PEZ_SOCK_ID_INVAL) {
        pez.thd[id].send_cnt ++;
    }
}

/*
 * router thread
 */
static void * pez_ipc_router_thread(void *arg) {
    void        *socket_router;
    pez_status  rc;
    char        buffer[INPROC_MAX_MSG_SIZE] = {0};
    char        trgt_id[PEZ_SOCK_ID_MAX_LEN] = {0};
    char        src_id[PEZ_SOCK_ID_MAX_LEN] = {0};
    int         i, recvd_size;
    int         len_src_id = -1, len_trgt_id = -1;

    /* socket type of router thread should be ZMQ_ROUTER */
    socket_router = zmq_socket(pez_ipc_get_zmq_ctx(), ZMQ_ROUTER);
    assert(socket_router != NULL);

    rc = zmq_bind(socket_router, INPROC_ADDRESS);
    assert(rc != -1);

    zmq_pollitem_t items [] = {
        {socket_router, 0, ZMQ_POLLIN, 0}
    };

    while (1) {
        zmq_poll(items, sizeof(items)/sizeof(zmq_pollitem_t), -1);
        if (items[0].revents & ZMQ_POLLIN) {
            /*
             * Recv msg
             */
            /* 1st: get ID frame */
            rc = zmq_recv (socket_router, &src_id, PEZ_SOCK_ID_MAX_LEN, 0);
            if (rc == -1) {
                printf("pez ipc: recv ID frame failed: %s\n", strerror(errno));
                continue;
            }
            len_src_id = rc;

            /* 2nd: Get dest id frame*/
            rc = zmq_recv (socket_router, &trgt_id, PEZ_SOCK_ID_MAX_LEN, 0);
            if (rc == -1) {
                printf("pez ipc: recv trgt id frame failed: %s\n",
                        strerror(errno));
                continue;
            }
            len_trgt_id = rc;

            /* 3rd: get real data */
            rc = zmq_recv (socket_router, buffer, INPROC_MAX_MSG_SIZE, 0);
            if (rc == -1) {
                printf("pez ipc: recv real data frame failed: %s\n", 
                        strerror(errno));
                continue;
            }
            recvd_size = rc;

            /*
             * Send msg
             */
            /* send ID frame */
            rc = zmq_send(socket_router, &trgt_id, len_trgt_id, ZMQ_SNDMORE);
            if (rc == -1) {
                printf("pez ipc: send id frame failed: %s\n", strerror(errno));
                continue;
            }

            /* send real data */
            rc = zmq_send(socket_router, buffer, recvd_size, 0);
            if (rc == -1) {
                printf("pez ipc: send data frame failed: %s\n",
                        strerror(errno));
                continue;
            }
            /* Count */
            pez_ipc_router_count(trgt_id, src_id);
        }
    }
}

/*
 * Create router thread. It should be invoked only once.
 */
static pez_status
pez_ipc_create_router_thread() {
    pez_status rc;

    rc = pthread_create(&pez.tid_router, NULL, pez_ipc_router_thread, NULL);
    if (rc != 0) {
        printf("pez ipc: create router thread failed: %s\n", strerror(errno));
        return rc;
    }
    return EOK;
}

/*
 * Do internal initialization and thread creation.
 * router thread takes charge of messages routing.
 */
void
pez_ipc_init() {
    pez_status rc;
    rc = pthread_mutex_init(&pez.lock, NULL);
    assert(rc == 0);

    rc = pez_ipc_create_router_thread();
    assert(rc == EOK);
}


