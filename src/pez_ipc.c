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

typedef struct {
    void                *zmq_ctx;
    pthread_t           tid_router;
    unsigned int        thread_num;
    pthread_mutex_t     lock;
    struct ev_zsock_t   pez_ev_zsock[1024];
} pez_t;

static pez_t pez;


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
 * Set linux thread id.
 * Do id and pid map here because:
 *      1. ZMQ_DEALER is not thread safe. With this map we can check whether
 *         the source ID is corrent when sending messages.
 *      2. If pid isn't exist routher thread can discard messages for this pid.
 */
static pez_status
pez_ipc_set_pid(int id)
{
    return EOK;
}

/*
 * Send msg to router thread. router thread will route it.
 * TODO: Broadcasting message should be added.
 */
pez_status
pez_ipc_msg_send (int trgt, int src, void *buf, size_t size) {
    pez_status rtn;

    if(!buf) {
        return EINVAL;
    }

    if ( (pez.pez_ev_zsock[src].pid != pthread_self()) ||
         (pez.pez_ev_zsock[trgt].pid == 0) ) {
        printf("target thread isn't exist or src is incorrect\n");
        return EINVAL;
    }

    /* 1st: send target id frame */
    rtn = zmq_send(pez.pez_ev_zsock[src].zsock,
                   &trgt,
                   sizeof(trgt),
                   ZMQ_SNDMORE);
    //printf("%s:id frame size:%d\n", __func__, rtn);
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
pez_ipc_msg_recv(void *socket, void *buf, size_t buffer_size, size_t *rtn_size) {
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
 * Didn't create zmq socket. Monitor only
 */
pez_status
pez_ipc_thread_init_tx(int id) {
    void * socket;
    int rc;
    void *zmq_ctx;

    if (id < 0) {
        printf("invalid id recvd\n");
        return EINVAL;
    }

    if (pez.pez_ev_zsock[id].pid != 0) {
        printf("this id was used alread\n");
        return EINVAL;
    } else {
        pez.pez_ev_zsock[id].pid = pthread_self();
    }

    zmq_ctx = pez_ipc_get_zmq_ctx();

    /* created socket will be stored to pez_ev_zsock[id] */
    socket = zmq_socket(zmq_ctx, ZMQ_DEALER);
    if (socket == NULL) {
        printf("unable to create ZMQ_DEALER socket: %s\n", strerror(errno));
        return errno;
    }

    /* set zmq id */
    rc = zmq_setsockopt (socket,
                         ZMQ_IDENTITY,
                         &id,
                         sizeof(int));
    if (rc == -1) {
        printf("unable to set ZMQ ID for thread %d:%s\n",
                    id,
                    strerror(errno));
        return errno;
    }

    /* connect to router thread */
    rc = zmq_connect(socket, INPROC_ADDRESS);
    if (rc == -1) {
        printf("unable to connect router for thread %d:%s\n",
                    id,
                    strerror(errno));
        return errno;
    }

    /* save socket to pez */
    pez.pez_ev_zsock[id].zsock = socket;

    return EOK;
}


/*
 * Do zmq socket creation and connect.
 */
pez_status
pez_ipc_thread_init_rx(struct ev_loop *loop, int id, ev_zsock_cbfn cb) {
    void * socket;
    int rc;
    void *zmq_ctx;

    zmq_ctx = pez_ipc_get_zmq_ctx();

    if (zmq_ctx == NULL || cb == NULL) {
        printf("NULL zmq_ctx or cb func recvd\n");
        return EINVAL;
    }

    if (pez.pez_ev_zsock[id].pid != 0) {
        printf("this id was used alread\n");
        return EINVAL;
    } else {
        pez.pez_ev_zsock[id].pid = pthread_self();
    }

    /* created socket will be stored to pez_ev_zsock[id] */
    socket = zmq_socket(zmq_ctx, ZMQ_DEALER);
    if (socket == NULL) {
        printf("unable to create ZMQ_DEALER socket: %s\n", strerror(errno));
        return errno;
    }

    /* set zmq id */
    rc = zmq_setsockopt (socket,
                         ZMQ_IDENTITY,
                         &id,
                         sizeof(int));
    if (rc == -1) {
        printf("unable to set ZMQ ID for thread %d:%s\n",
                    id,
                    strerror(errno));
        return errno;
    }

    /* connect to router thread */
    rc = zmq_connect(socket, INPROC_ADDRESS);
    if (rc == -1) {
        printf("unable to connect router for thread %d:%s\n",
                    id,
                    strerror(errno));
        return errno;
    }

    /* Only need EV_READ event to read incoming msg */
    ev_zsock_init(&pez.pez_ev_zsock[id], cb, socket, EV_READ);
    ev_zsock_start(loop, &pez.pez_ev_zsock[id]);

    return EOK;
}

static pez_status
pez_ipc_router_thread_recv_msg() {
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
        if (items[0].revents & ZMQ_POLLIN) {
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
 * Do internal initialization and thread creation.
 * router thread takes charge of messages routing.
 */
void
pez_ipc_init() {
    int rc;
    rc = pthread_mutex_init(&pez.lock, NULL);
    assert(rc == 0);

    rc = pez_ipc_create_router_thread();
    assert(rc == EOK);
}


