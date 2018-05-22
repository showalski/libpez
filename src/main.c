#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "pez_ipc.h"
#include <ev.h>
#include <zmq.h>
#include "ev_zsock.h"
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include "msg.pb-c.h"

/*
 * send heart beat message
 */
static status
send_heart_beat_message(const char *trgt, const char *src, char *str) {
    Msg msg = MSG__INIT;
    HeartBeat hb_msg = HEART_BEAT__INIT;
    size_t len;
    void *buf = NULL;
    status rc = EOK;

    if (!src || !trgt) {
        printf("%s: invalid src or trgt\n", __func__);
        rc = EINVAL;
        goto end;
    }
    if (!str) {
        printf("%s: invalid str recvd\n", __func__);
    }

    hb_msg.substr = str;
    msg.type = MSGTYPE__HEARTBEAT;
    msg.hb = &hb_msg;
    msg.src = (char *)src;
    msg.trgt = (char *)trgt;

    len = msg__get_packed_size(&msg);
    buf = malloc(len);
    if (!buf) {
        printf("%s: unable to alloc mem\n", __func__);
        rc = ENOMEM;
        goto end;
    }
    msg__pack(&msg, buf);

    rc = pez_ipc_msg_send (trgt, src, buf, len);
    if (rc != EOK) {
        printf("%s: failed to send hb from %s to %s\n",
                __func__,
                src,
                trgt);
        goto end;
    }

end:
    if (buf) {
        free(buf);
    }
    return rc;
}

static status
common_msg_handler(void * thread_name, uint8_t *buf, size_t size) {
    Msg *msg;

    if (!thread_name || !buf || size <= 0) {
        return EINVAL;
    }

    msg = msg__unpack(NULL, size, buf);
    if (msg == NULL) {
        printf("%s:%s failed to unpack messag\n", __func__, thread_name);
        return EINVAL;
    }

    switch (msg->type) {
        case MSGTYPE__HEARTBEAT:
            printf("%s recvd heartbeat msg from %s, val:%s\n",
                    thread_name,
                    msg->src,
                    msg->hb->substr);
            break;
        default:
            printf("%s:%s unknown msg type\n", __func__, thread_name);
    }

    msg__free_unpacked(msg, NULL);
    return EOK;
}

/*
 * main thread ipc handler
 */
static void
main_thread_ipc_handler(struct ev_loop *loop, ev_zsock_t *wz, int revents) {
    void *socket;
    socket = wz->zsock;
    status rc;
    uint8_t buffer[MSG_BUF_SIZE] = {0};
    size_t size;

    rc = pez_ipc_msg_recv(socket, buffer, MSG_BUF_SIZE, &size);
    if (rc != EOK) {
        printf("%s: main thread failed to recv message\n", __func__);
    }

    rc = common_msg_handler("main thread", buffer, size);
    if (rc != EOK) {
        printf("%s: main thread failed to parse msg\n", __func__);
    }
}

static void
foo_thread_ipc_handler(struct ev_loop *loop, ev_zsock_t *wz, int revents) {
    void *socket;
    socket = wz->zsock;
    status rc;
    uint8_t buffer[MSG_BUF_SIZE] = {0};
    size_t size;

    rc = pez_ipc_msg_recv(socket, buffer, MSG_BUF_SIZE, &size);
    if (rc != EOK) {
        printf("%s: foo thread failed to recv message\n", __func__);
    }

    rc = common_msg_handler("foo thread", buffer, size);
    if (rc != EOK) {
        printf("%s: foo thread failed to parse msg\n", __func__);
    }
}

static void
bar_thread_ipc_handler(struct ev_loop *loop, ev_zsock_t *wz, int revents) {
    void *socket;
    socket = wz->zsock;
    status rc;
    uint8_t buffer[MSG_BUF_SIZE] = {0};
    size_t size;

    rc = pez_ipc_msg_recv(socket, buffer, MSG_BUF_SIZE, &size);
    if (rc != EOK) {
        printf("%s: bar thread failed to recv message\n", __func__);
    }

    rc = common_msg_handler("bar thread", buffer, size);
    if (rc != EOK) {
        printf("%s: bar thread failed to parse msg\n", __func__);
    }
}

static void
main_timeout_cb (struct ev_loop *loop, ev_timer *w, int revents) {
    int rc;
    char buffer[MSG_BUF_SIZE] = {1, 2, 3, 4};

    rc = send_heart_beat_message("foo",
                                 "main",
                                 "this is main");
    if (rc != EOK) {
        printf("%s: foo failed to send hb msg\n", __func__);
    }

    rc = send_heart_beat_message("bar",
                                 "main",
                                 "this is main");
    if (rc != EOK) {
        printf("%s: foo failed to send hb msg\n", __func__);
    }
}

static void
foo_thread_timeout_cb (struct ev_loop *loop, ev_timer *w, int revents) {
    status rc;

    rc = send_heart_beat_message("main",
                                 "foo",
                                 "this is foo");
    if (rc != EOK) {
        printf("%s: foo failed to send hb msg\n", __func__);
    }
}

static void
bar_thread_timeout_cb (struct ev_loop *loop, ev_timer *w, int revents) {
    status rc;

    rc = send_heart_beat_message("main",
                                 "bar",
                                 "this is bar");
    if (rc != EOK) {
        printf("%s: foo failed to send hb msg\n", __func__);
    }
}

/*
 * foo thread
 */
void * pez_foo_thread (void *arg) {
    int rc;
    ev_timer timeout_watcher;
    status rtn = EOK;
    struct ev_loop *loop = ev_loop_new (0);
    assert (loop != NULL);

    rc = pez_ipc_thread_init_rx(loop, "foo", foo_thread_ipc_handler);
    if (rc != EOK) {
        printf("foo thread failed to init ipc\n");
        return NULL;
    }

    ev_timer *p_timeout_watcher = &timeout_watcher;
    ev_timer_init (p_timeout_watcher, foo_thread_timeout_cb, 1.0, 1.0);
    ev_timer_start (loop, &timeout_watcher);

    ev_run (loop, 0);
    printf("foo thread exited\n");

    return NULL;
}

/*
 * foo thread
 */
void * pez_bar_thread (void *arg) {
    int rc;
    ev_timer timeout_watcher;
    status rtn = EOK;
    struct ev_loop *loop = ev_loop_new (0);
    assert (loop != NULL);

    rc = pez_ipc_thread_init_rx(loop, "bar", bar_thread_ipc_handler);
    if (rc != EOK) {
        printf("bar thread failed to init ipc\n");
        return NULL;
    }

    ev_timer *p_timeout_watcher = &timeout_watcher;
    ev_timer_init (p_timeout_watcher, bar_thread_timeout_cb, 1.0, 1.0);
    ev_timer_start (loop, &timeout_watcher);

    ev_run (loop, 0);
    printf("bar thread exited\n");

    return NULL;
}

/*
 * main thread
 */
int main() {
    status rtn = EOK;
    pthread_t foo_tid;
    pthread_t bar_tid;
    struct ev_loop *loop;

    loop = ev_default_loop (0);

    pez_ipc_init();

    /* Enable debug */
    //pez_ipc_enable_debug();

    rtn = pez_ipc_thread_init_rx(loop, "main", main_thread_ipc_handler);
    if (rtn != EOK) {
        printf("main thread failed to init ipc\n");
        return -1;
    }

    /* create threads */
    pthread_create(&foo_tid, NULL, pez_foo_thread, NULL);
    pthread_create(&bar_tid, NULL, pez_bar_thread, NULL);


    ev_timer timeout_watcher;
    ev_timer *p_timeout_watcher = &timeout_watcher;
    ev_timer_init (p_timeout_watcher, main_timeout_cb, 1.0, 1.0);
    ev_timer_start (loop, &timeout_watcher);

    ev_run (loop, 0);
    printf("loop exited\n");
}


