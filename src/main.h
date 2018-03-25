#ifndef MAIN_H
#define MAIN_H
#include <errno.h>
typedef int    status;

/*
 * New messages should be added at here
 */
#define FOREACH_MSG(CMD)        \
        CMD(MAIN_MSG_NULL, 1)     \
        CMD(MAIN_MSG_INVALID, 2)     \
        CMD(MAIN_MSG_MSG1, 3)     \
        CMD(MAIN_MSG_MSG2, 4)     \
        CMD(MAIN_MSG_MAX, MAIN_MSG_MSG2)

#define GENERATE_ENUM(MSG, VALUE)      MSG = VALUE,

#define GENERATE_STRING(MSG, VALUE)    #MSG,

#define GENERATE_BUFFER_INIT_VALUE(MSG, VALUE)    NULL,

#define GENERATE_INIT_PID_VALUE(MSG, VALUE)     0,

/*
 * Enum definition of each msg
 */
typedef enum {
    FOREACH_MSG(GENERATE_ENUM)
}MAIN_MSG_TYPE;

/*
 * New thread should be added at here
 */
#define FOREACH_THREAD(CMD)        \
        CMD(MAIN_THREAD_ALL, 0)     \
        CMD(MAIN_THREAD_EXTERNAL, 1)     \
        CMD(MAIN_THREAD_MAIN, 2)     \
        CMD(MAIN_THREAD_FOO, 3)     \
        CMD(MAIN_THREAD_BAR, 4)     \
        CMD(MAIN_THREAD_MAX, MAIN_THREAD_BAR)

/*
 * Enum definition of each thread
 */
typedef enum {
    FOREACH_THREAD(GENERATE_ENUM)
}MAIN_THREAD_TYPE;

char * thread_str[] = {
    FOREACH_THREAD(GENERATE_STRING)
};

#define EOK     0

#define MSG_BUF_SIZE    1024

#endif /* MAIN_H */


