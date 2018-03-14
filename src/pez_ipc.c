#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zmq.h>
#include <pthread.h>
#include "msg.pb-c.h"
#include "pez_common.h"
#include "pez_ipc.h"
#include "ev_zsock.h"
#ifdef __APPLE__
#include <mach/error.h>
#else
#include <error.h>
#endif
 
typedef struct {
    void                *zmq_ctx;
    pthread_t           tid_router;
    unsigned int        thread_num;
    pthread_mutex_t     lock;
    struct ev_zsock_t   pez_ev_zsock[1024];
} pez_t;
 
static pez_t pez;
 
 
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
pez_ipc_msg_send (int trgt, int src, void *buf, int size) {
    pez_status rtn;
 
    if(!buf) {
        return EINVAL;
    }
 
    /* 1st: send target id frame */
    rtn = zmq_send(pez.pez_ev_zsock[src].zsock,
                   &trgt,
                   sizeof(trgt),
                   ZMQ_SNDMORE);
    printf("%s:id frame size:%d\n", __func__, rtn);
    if (rtn == -1) {
        printf("%s: send trgt id frame failed: %s\n", __func__, strerror(errno));
        return rtn;
    }
 
    /* 2nd: send data frame */
    rtn = zmq_send(pez.pez_ev_zsock[src].zsock,
                   buf,
                   size,
                   0);
    if (rtn != size) {
        printf("%s: msg send failed(sent %d bytes)\n", __func__, rtn);
        return rtn;
    }
 
    return EOK;
}
 
/*
 * recv message
 */
pez_status
pez_ipc_msg_recv(void *socket, void *buf, int buffer_size, int *rtn_size) {
    int rc;
 
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
 * Do zmq socket creation and connect.
 */
pez_status
pez_ipc_thread_init(struct ev_loop *loop, int thread_id, ev_zsock_cbfn cb) {
    void * socket;
    int rc;
    void *zmq_ctx;
 
    zmq_ctx = pez_ipc_get_zmq_ctx();
 
    if (zmq_ctx == NULL || cb == NULL) {
        printf("NULL zmq_ctx or cb func recvd\n");
        return EINVAL;
    }
 
    /* created socket will be stored to pez_ev_zsock[thread_id] */
    socket = zmq_socket(zmq_ctx, ZMQ_DEALER);
    if (socket == NULL) {
        printf("unable to create ZMQ_DEALER socket: %s\n", strerror(errno));
        return errno;
    }
 
    /* set zmq id */
    rc = zmq_setsockopt (socket,
                         ZMQ_IDENTITY,
                         &thread_id,
                         sizeof(int));
    if (rc == -1) {
        printf("unable to set ZMQ ID for thread %d:%s\n",
                    thread_id,
                    strerror(errno));
        return errno;
    }
 
    /* connect to router thread */
    rc = zmq_connect(socket, INPROC_ADDRESS);
    if (rc == -1) {
        printf("unable to connect router for thread %d:%s\n",
                    thread_id,
                    strerror(errno));
        return errno;
    }
 
    /* Only need EV_READ event to read incoming msg */
    ev_zsock_init(&pez.pez_ev_zsock[thread_id], cb, socket, EV_READ);
    ev_zsock_start(loop, &pez.pez_ev_zsock[thread_id]);
 
    return EOK;
}
 
/*
 * router thread
 */
static void * pez_ipc_router_thread(void *arg) {
    void *socket_router;
    int rc;
    int more;
    size_t more_size = sizeof(more);
    char buffer[INPROC_MAX_MSG_SIZE] = {0};
    int i, recvd_size;
    int src = -1, trgt = -1;
 
    /* socket type of router thread should be ZMQ_ROUTER */
    socket_router = zmq_socket(pez_ipc_get_zmq_ctx(), ZMQ_ROUTER);
    assert(socket_router != NULL);
 
    rc = zmq_bind(socket_router, INPROC_ADDRESS);
    assert(rc != -1);
 
    zmq_pollitem_t items [] = {
        {socket_router, 0, ZMQ_POLLIN, 0}
    };
 
    while (1) {
        more = 1;
        zmq_poll(items, sizeof(items)/sizeof(zmq_pollitem_t), -1);
        //printf("*************ROUTER THREAD MSG RECVD************\n");
        if (items[0].revents & ZMQ_POLLIN) {
            /*
            while(more) {
                memset(buffer, 0, INPROC_MAX_MSG_SIZE);
                rc = zmq_recv(socket_router, buffer, INPROC_MAX_MSG_SIZE, 0);
                if (rc == -1) {
                    printf("router thread recvd err:%s\n", strerror(errno));
                    break;
                }
                printf("RECVD DATA:");
                for (i = 0; i < rc; i ++) {
                    printf("%x,", buffer[i]);
                }
                printf("\n");
                rc = zmq_getsockopt(socket_router, ZMQ_RCVMORE, &more, &more_size);
                if (rc == -1) {
                    printf("router thread getsocket err:%s\n", strerror(errno));
                    break;
                }
            }
            */
 
            /*
             * Recv msg
             */
            /* 1st: get ID frame */
            rc = zmq_recv (socket_router, &src, sizeof(int), 0);
            if (rc == -1) {
                printf("%s: recv ID frame failed: %s\n", __func__, strerror(errno));
                continue;
            }
 
            /* 2nd: Get dest id frame*/
            rc = zmq_recv (socket_router, &trgt, sizeof(int), 0);
            if (rc == -1) {
                printf("%s: recv trgt id frame failed: %s\n", __func__, strerror(errno));
                continue;
            }
 
            /* 3rd: get real data */
            rc = zmq_recv (socket_router, buffer, INPROC_MAX_MSG_SIZE, 0);
            if (rc == -1) {
                printf("%s: recv real data frame failed: %s\n", __func__, strerror(errno));
                continue;
            }
            recvd_size = rc;
 
            /*
             * Send msg
             */
            /* send ID frame */
            rc = zmq_send(socket_router, &trgt, sizeof(int), ZMQ_SNDMORE);
            if (rc == -1) {
                printf("%s: send id frame failed: %s\n", __func__, strerror(errno));
                continue;
            }
 
            /* send real data */
            rc = zmq_send(socket_router, buffer, recvd_size, 0);
            if (rc == -1) {
                printf("%s: send data frame failed: %s\n", __func__, strerror(errno));
                continue;
            }
 
        }
    }
}
 
/*
 * Create router thread. It should be invoked only once.
 */
static pez_status
pez_ipc_create_router_thread() {
    int rc;
 
    rc = pthread_create(&pez.tid_router, NULL, pez_ipc_router_thread, NULL);
    if (rc != 0) {
        printf("%s: create router thread failed: %s\n", __func__, strerror(errno));
        return rc;
    }
 
    return EOK;
}
 
/*
 *
 */
void
pez_ipc_init() {
    int rc;
    rc = pthread_mutex_init(&pez.lock, NULL);
    assert(rc == 0);
 
    rc = pez_ipc_create_router_thread();
    assert(rc == EOK);
}


