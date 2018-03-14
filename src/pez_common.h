#ifndef PEZ_COMMON_H
#define PEZ_COMMON_H
#include <errno.h>
typedef int    status_t;
 
#define INPROC_ADDRESS          "inproc://channel"
 
#define PEZ_THREAD_ID_MAX_LEN     255
 
/*
* New messages should be added at here
*/
#define FOREACH_MSG(CMD)        \
        CMD(PEZ_MSG_NULL, 1)     \
        CMD(PEZ_MSG_INVALID, 2)     \
        CMD(PEZ_MSG_MSG1, 3)     \
        CMD(PEZ_MSG_MSG2, 4)     \
        CMD(PEZ_MSG_MAX, PEZ_MSG_MSG2)
 
#define GENERATE_ENUM(MSG, VALUE)      MSG = VALUE,
 
#define GENERATE_STRING(MSG, VALUE)    #MSG,
 
#define GENERATE_BUFFER_INIT_VALUE(MSG, VALUE)    NULL,
 
#define GENERATE_INIT_PID_VALUE(MSG, VALUE)     0,
 
/*
* Enum definition of each msg
*/
typedef enum {
    FOREACH_MSG(GENERATE_ENUM)
}PEZ_MSG_TYPE;
 
/*
* New thread should be added at here
*/
#define FOREACH_THREAD(CMD)        \
        CMD(PEZ_THREAD_ALL, 0)     \
        CMD(PEZ_THREAD_EXTERNAL, 1)     \
        CMD(PEZ_THREAD_MAIN, 2)     \
        CMD(PEZ_THREAD_FOO, 3)     \
        CMD(PEZ_THREAD_MAX, PEZ_THREAD_FOO)
 
/*
* Enum definition of each thread
*/
typedef enum {
    FOREACH_THREAD(GENERATE_ENUM)
}PEZ_THREAD_TYPE;
 
 
#define EOK     0
 
#endif /* PEZ_COMMON_H */


