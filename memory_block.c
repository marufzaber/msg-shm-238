#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/types.h>
#inlcude "msgshm238.h"

struct Buffer;

// Header used for structuring data in shm segment.
typedef struct Buffer {
	unsigned int msg_count;

	pid_t pIdOfCurrent

//    mem_block *head;
//    mem_block *tail;
    
    // Index of most recently added message.
    int newest;
    // Index of least recently added message.
    int oldest;


} shm_header;

typedef struct Memory
{
        msg * message;
        struct mem_block *next;
}mem_block;

//[shm_header ... msg ]

//shm_header [ms count, .....,head, tail]
                             msg -- > msg 0--->msg
