#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/types.h>
#inlcude "msgshm238.h"

struct Buffer;

// Encapsulates the information necessary to send a message to a process.
typedef struct Buffer {
	unsigned int mem_count; 

	pid_t pIdOfCurrent

    mem_block *head;
    mem_block *tail;


} shr_mem;

typedef struct Memory
{
        msg message;
        struct mem_block *ptr;
}mem_block;

 
