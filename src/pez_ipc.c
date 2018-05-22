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

#define PEZ_THREAD_MAX_NUM        (1024)
#define PEZ_THREAD_ID_MAX_LEN     (32)
#define PEZ_THREAD_ID_INVAL       (-1)

#define PEZ_STRING_1_LINE_LEN     (60)
#define PEZ_STRING_SUFFIX_LEN     (PEZ_THREAD_ID_MAX_LEN * 3)

typedef struct {
    pthread_t           tid;
    struct ev_zsock_t   pez_ev_zsock;
    char                identity[PEZ_THREAD_ID_MAX_LEN];
    uint64_t            recv_cnt;       /* increase by thread itself */
    uint64_t            snd_cnt;        /* increate by thread itself */
    uint64_t            rt_recv_cnt;    /* increase by router */
    uint64_t            rt_snd_cnt;     /* increase by router */
} pez_thd_t;

typedef struct {
    void                *zmq_ctx;
    pthread_t           tid_router;
    unsigned int        thread_num;
    pthread_mutex_t     lock;
    pez_thd_t           thd[PEZ_THREAD_MAX_NUM];
} pez_t;

static pez_t pez;

static int pez_debug_flag = 0;

/*
 * Enable debug
 */
void
pez_ipc_enable_debug() {
    pez_debug_flag = 1;
}

/*
 * Disable debug
 */
void
pez_ipc_disable_debug() {
    pez_debug_flag = 0;
}

/*
 * sprintf without null terminator
 */
static int
pez_ipc_sprintf_nonull(char *str, const char *format, ...)
{
    int i;
    va_list va;
    va_start (va, format);
    i = vsprintf (str, format, va);
    va_end (va);
    str[strlen(str)] = ' ';
    return i;
}

/*
 * hexdump data. Like linux hexdump tool.
 * In multi-thread env it's better to have special suffix prepended before
 * each data print.
 */
void
pez_ipc_hexdump(const char *suffix, const char* data, size_t len)
{
    uint32_t    i, r, c;
    uint8_t     buf[PEZ_STRING_1_LINE_LEN];
    char        *p = (char *)buf;
    if (!data || !suffix) {
        return;
    }
    for (r = 0, i = 0; r < (len / 16 + (len % 16 != 0)); r ++,i += 16) {
        p = (char *)buf;
        memset(p, ' ', PEZ_STRING_1_LINE_LEN);
        buf[PEZ_STRING_1_LINE_LEN - 1] = '\0';
        /* location of first byte in line */
        p += pez_ipc_sprintf_nonull(p, "%04X: ",i);
        /* left half of hex dump */
        for (c = i; c < i + 8; c ++) {
            if (c < len) {
                p += pez_ipc_sprintf_nonull(p,"%02X",
                                            ((unsigned char const *)data)[c]);
            } else {
                p += 2;         /* 2 numbers */
            }
        }
        p ++;                   /* one space */
        /* right half of hex dump */
        for (c = i + 8; c < i + 16; c ++) {
            if (c < len) {
                p += pez_ipc_sprintf_nonull(p, "%02X",
                                            ((unsigned char const *)data)[c]);
            } else {
                p += 2;         /* 2 numbers */
            }
        }
        p ++;                   /* one space */
        /* ASCII dump */
        for (c = i; c < i + 16; c ++) {
            if (c < len) {
                if (((unsigned char const *)data)[c] >= 32 &&
                    ((unsigned char const *)data)[c] < 127) {
                    p += pez_ipc_sprintf_nonull(p, "%c",
                                                 ((char const *)data)[c]);
                } else {
                    /* put this for non-printables */
                    p += pez_ipc_sprintf_nonull(p, ".");
                }
            } else {
                p += 1;
            }
        }
        printf("%s| %s\n", buf, suffix);
    }
}

/*
 * Print counters managed by router thread.
 */
void
pez_ipc_router_counter_print()
{
    int32_t     i;
    for (i = 0; i < PEZ_THREAD_MAX_NUM; i ++) {
        if (strnlen(pez.thd[i].identity, PEZ_THREAD_ID_MAX_LEN) != 0) {
            printf("rt counter:%s: recv:%llu, send:%llu\n",
                         pez.thd[i].identity,
                         pez.thd[i].rt_recv_cnt,
                         pez.thd[i].rt_snd_cnt);
        }
    }
}

/*
 * Find numerical id for given str. If it isn't in there PEZ_THREAD_ID_INVAL
 * will be returned.
 */
static void
pez_ipc_index_get_bystr(const char *str, int32_t *id) {
    int32_t i = 0;

    for ( ; i < PEZ_THREAD_MAX_NUM; i ++) {
        if (!strncmp(pez.thd[i].identity, str, PEZ_THREAD_ID_MAX_LEN)) {
            *id = i;
            break;
        }
    }
    if (i == PEZ_THREAD_MAX_NUM) {
        *id = PEZ_THREAD_ID_INVAL;
    }
}

/*
 * Find numerical id for given linux thread id.
 */
static void
pez_ipc_index_get_bythdid(pthread_t tid, int32_t *id)
{
    int32_t i = 0;
    for ( ; i < pez.thread_num; i ++) {
        if (pez.thd[i].tid == tid) {
            *id = i;
            break;
        }
    }
    if (i == pez.thread_num) {
        *id = PEZ_THREAD_ID_INVAL;
    }
}
/*
 * Find registered identity based on current thread id
 */
char *
pez_ipc_identity_get()
{
    int32_t     i = 0;
    pthread_t   tid;
    char        *p = "null";
    tid = pthread_self();
    for ( ; i < pez.thread_num; i ++) {
        if (pez.thd[i].tid == tid) {
            p = pez.thd[i].identity;
            break;
        }
    }
    if (i == pez.thread_num) {
        p = "NULL";
    }
    return p;
}

/*
 * Increase counters
 */
static void
pez_ipc_router_count(char *trgt, char *src)
{
    int32_t     id = 0;;
    pez_ipc_index_get_bystr(trgt, &id);
    if (id != PEZ_THREAD_ID_INVAL) {
        pez.thd[id].rt_recv_cnt ++;
    }
    pez_ipc_index_get_bystr(src, &id);
    if (id != PEZ_THREAD_ID_INVAL) {
        pez.thd[id].rt_snd_cnt ++;
    }
}

/*
 * Allocate numerical value for specific string. If no room for that string
 * PEZ_THREAD_ID_INVAL will be returned.
 */
static void
pez_ipc_index_alloc(const char *str, int32_t *id) {
    int32_t     i = 0;
    pthread_mutex_lock(&pez.lock);
    for ( ; i < PEZ_THREAD_MAX_NUM; i ++) {
        if (strnlen(pez.thd[i].identity, PEZ_THREAD_ID_MAX_LEN) == 0) {
            strncpy(pez.thd[i].identity, str, PEZ_THREAD_ID_MAX_LEN);
            *id = i;
            pez.thread_num ++;
            //printf("pez ipc: alloc id %d for %s\n", i, str);
            break;
        }
    }
    if (i == PEZ_THREAD_MAX_NUM) {
        *id = PEZ_THREAD_ID_INVAL;
    }
    pthread_mutex_unlock(&pez.lock);
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
    char        suffix[PEZ_STRING_SUFFIX_LEN] = {0};

    if(!buf || !trgt || !src) {
        return EINVAL;
    }

    pez_ipc_index_get_bystr(trgt, &trgt_id);
    if (trgt_id == PEZ_THREAD_ID_INVAL) {
        printf("pez ipc: invalid trgt thread name(%s)\n", trgt);
        return EINVAL;
    }
    pez_ipc_index_get_bystr(src, &src_id);
    if (src_id == PEZ_THREAD_ID_INVAL) {
        printf("pez ipc: invalid src thread name(%s)\n", src);
        return EINVAL;
    }

    if (pez.thd[src_id].tid != pthread_self()) {
        printf("pez ipc:src is incorrect\n");
        return EINVAL;
    }

    /* 1st: send target id frame */
    rtn = zmq_send(pez.thd[src_id].pez_ev_zsock.zsock,
                   trgt,
                   strnlen(trgt,PEZ_THREAD_ID_MAX_LEN),
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

    /* count sent msg number. Count only by thread itself, no lock needed */
    pez.thd[src_id].snd_cnt ++;
    if (pez_debug_flag) {
        snprintf(suffix, PEZ_STRING_SUFFIX_LEN, "pez msg snd(%s)", src);
        pez_ipc_hexdump(suffix, buf, size);
        printf("%s: snd cnt: %llu\n", src, pez.thd[src_id].snd_cnt);
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
    int32_t     id = 0;
    pez_status  rc;
    char        suffix[PEZ_STRING_SUFFIX_LEN] = {0};

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

    /* count recv msg number. No lock needed */
    pez_ipc_index_get_bythdid(pthread_self(), &id);
    pez.thd[id].recv_cnt ++;
    if (pez_debug_flag) {
        snprintf(suffix, PEZ_STRING_SUFFIX_LEN, "pez msg recv(%s)",
                 pez.thd[id].identity);
        pez_ipc_hexdump(suffix, buf, *rtn_size);
        printf("%s: recv cnt: %llu\n",
                     pez.thd[id].identity,
                     pez.thd[id].recv_cnt);
    }

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

    pez_ipc_index_get_bystr(tx_id, &id);
    if (id != PEZ_THREAD_ID_INVAL) {
        printf("pez ipc: don't invoke this API twice for same id. Previous"
               " call is by %s\n", pez.thd[id].identity);
        return EINVAL;
    }

    pez_ipc_index_alloc(tx_id, &id);
    if (id == PEZ_THREAD_ID_INVAL) {
        printf("pez ipc: no room for new tx thread(%s) allocation\n", tx_id);
        return ENOMEM;
    }

    pez.thd[id].tid = pthread_self();

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

    pez_ipc_index_get_bystr(rx_id, &id);
    if (id != PEZ_THREAD_ID_INVAL) {
        printf("pez ipc: don't invoke this API twice for same id. Previous"
               " call is by %s\n", pez.thd[id].identity);
        return EINVAL;
    }

    pez_ipc_index_alloc(rx_id, &id);
    if (id == PEZ_THREAD_ID_INVAL) {
        printf("pez ipc: no room for new rx thread(%s) allocation\n", rx_id);
        return ENOMEM;
    }

    pez.thd[id].tid = pthread_self();

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
                         strnlen(rx_id,PEZ_THREAD_ID_MAX_LEN));
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
 * router thread
 */
static void * pez_ipc_router_thread(void *arg) {
    void        *socket_router;
    pez_status  rc;
    char        buffer[INPROC_MAX_MSG_SIZE] = {0};
    char        trgt_id[PEZ_THREAD_ID_MAX_LEN] = {0};
    char        src_id[PEZ_THREAD_ID_MAX_LEN] = {0};
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
            rc = zmq_recv (socket_router, &src_id, PEZ_THREAD_ID_MAX_LEN, 0);
            if (rc == -1) {
                printf("pez ipc: recv ID frame failed: %s\n", strerror(errno));
                continue;
            }
            len_src_id = rc;

            if (pez_debug_flag) {
                pez_ipc_hexdump("rt(src id)",
                                 (const char*)src_id,
                                 len_src_id);
            }

            /* 2nd: Get dest id frame*/
            rc = zmq_recv (socket_router, &trgt_id, PEZ_THREAD_ID_MAX_LEN, 0);
            if (rc == -1) {
                printf("pez ipc: recv trgt id frame failed: %s\n",
                        strerror(errno));
                continue;
            }
            len_trgt_id = rc;

            if (pez_debug_flag) {
                pez_ipc_hexdump("rt(trgt id)",
                                 (const char*)trgt_id,
                                 len_trgt_id);
            }

            /* 3rd: get real data */
            rc = zmq_recv (socket_router, buffer, INPROC_MAX_MSG_SIZE, 0);
            if (rc == -1) {
                printf("pez ipc: recv real data frame failed: %s\n", 
                        strerror(errno));
                continue;
            }
            recvd_size = rc;

            if (pez_debug_flag) {
                pez_ipc_hexdump("rt(data)",
                                 (const char*)buffer,
                                 recvd_size);
            }

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

            if (pez_debug_flag) {
                pez_ipc_router_counter_print();
            }
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


