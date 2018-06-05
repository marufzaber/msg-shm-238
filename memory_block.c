#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/types.h>
#inlcude "msgshm238.h"

struct Buffer;

// Encapsulates the information necessary to send a message to a process.
typedef struct Buffer {
	unsigned int msg_count;

	pid_t pIdOfCurrent

    mem_block *head;
    mem_block *tail;


} shm_header;

typedef struct Memory
{
        msg * message;
        struct mem_block *next;
}mem_block;

[shm_header ... msg ]

shm_header [ms count, .....,head, tail]
                             msg -- > msg 0--->msg
