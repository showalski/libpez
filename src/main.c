#include <stdio.h>
#include "pez_common.h"
#include "pez_ipc.h"
#include <ev.h>
#include <zmq.h>
#include "ev_zsock.h"
#include <pthread.h>
#include <assert.h>
#include <string.h>
 
 
/*
* main thread ipc handler
*/
void main_thread_ipc_handler(struct ev_loop *loop, ev_zsock_t *wz, int revents) {
    void *socket;
    socket = wz->zsock;
    int rc;
    char buffer[1024] = {0};
    int size;
 
    rc = pez_ipc_msg_recv(socket, buffer, 1024, &size);
    if (rc != EOK) {
        printf("%s: main thread failed to recv message\n", __func__);
    }
 
    int i;
    for (i = 0; i < size; i ++) {
        printf("%x %c", buffer[i], buffer[i]);
    }
    printf("\n%s: size %d\n", __func__, size);
 
 
}
 
void foo_thread_ipc_handler(struct ev_loop *loop, ev_zsock_t *wz, int revents) {
    void *socket;
    socket = wz->zsock;
    int rc;
    char buffer[1024] = {0};
    int size;
  
    rc = pez_ipc_msg_recv(socket, buffer, 1024, &size);
    if (rc != EOK) {
        printf("%s: foo thread failed to recv message\n", __func__);
    }
 
    int i;
    for (i = 0; i < size; i ++) {
        printf("%x %c", buffer[i], buffer[i]);
    }
    printf("\n%s: size %d\n", __func__, size);
 

 
 
}
 
void main_timeout_cb (struct ev_loop *loop, ev_timer *w, int revents) {
    int rc;
    char buffer[1024] = {1, 2, 3, 4};
 
    rc = pez_ipc_msg_send (PEZ_THREAD_FOO, PEZ_THREAD_MAIN, "FOOBAR", 6);
    if (rc != EOK) {
        printf("%s: main thread failed to send message\n", __func__);
    }
 
}
 
void foo_thread_timeout_cb (struct ev_loop *loop, ev_timer *w, int revents) {
}
 
 
/*
* foo thread
*/
void * pez_foo_thread (void *arg) {
    int rc;
    ev_timer timeout_watcher;
    status_t rtn = EOK;
    struct ev_loop *loop = ev_loop_new (0);
    assert (loop != NULL);
 
    rc = pez_ipc_thread_init(loop, PEZ_THREAD_FOO, foo_thread_ipc_handler);
    if (rc != EOK) {
        printf("main thread failed to init ipc\n");
        return NULL;
    }
 
    ev_timer *p_timeout_watcher = &timeout_watcher;
    ev_timer_init (p_timeout_watcher, foo_thread_timeout_cb, 1.0, 1.0);
    ev_timer_start (loop, &timeout_watcher);
 
    ev_run (loop, 0);
    printf("test1 thread exited\n");
 
    return NULL;
}
 
/*
* main thread
*/
int main() {
    status_t rtn = EOK;
    pthread_t tid;
    ev_zsock_t wz;
    struct ev_loop *loop;
    int rc;
 
    loop = ev_default_loop (0);
 
    pez_ipc_init();
 
    rc = pez_ipc_thread_init(loop, PEZ_THREAD_MAIN, main_thread_ipc_handler);
    if (rc != EOK) {
        printf("main thread failed to init ipc\n");
        return -1;
    }
 
    /* create threads */
    pthread_create(&tid, NULL, pez_foo_thread, NULL);
    //pthread_create(&tid, NULL, pez_test2_thread, NULL);
    //pthread_create(&tid, NULL, pez_ipc_router_thread, NULL);
 
 
    ev_timer timeout_watcher;
    ev_timer *p_timeout_watcher = &timeout_watcher;
    ev_timer_init (p_timeout_watcher, main_timeout_cb, 1.0, 1.0);
    //timeout_watcher.data = socket_dealer_main;
    ev_timer_start (loop, &timeout_watcher);
 
 
    ev_run (loop, 0);
    printf("loop exited\n");
 
}


