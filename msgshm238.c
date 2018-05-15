#include <stdio.h>
#include <stdlib.h>
#include "msgshm238.h"
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h> /* O_* constnats */
#include <sys/stat.h> /* mode constants */
#include <sys/mman.h>
#include <limits.h>
#include "uthash.h" /* Hash table provided by https://troydhanson.github.io/uthash/ */

#define PO10_LIMIT (INT_MAX/10)
/**
 * For finding the string length of an integer.
 * Credit goes to: https://stackoverflow.com/a/4143288/1214974
 */
int nDigits(int i)
{
    int n,po10;
    if (i < 0) i = -i;
    n=1;
    po10=10;
    while(i>=po10)
    {
        n++;
        if (po10 > PO10_LIMIT) break;
        po10*=10;
    }
    return n;
}


/*
 * Useful tutorial on POSIX shared memory:
 * http://man7.org/training/download/posix_shm_slides.pdf
 */

/**
 * Manually cache the pid to avoid a system call for each send.
 * Apparently the library did provide automatic caching of the pid in some versions, but this caused bugs.
 * See http://man7.org/linux/man-pages/man2/getpid.2.html
 */
pid_t senderId = -42;

/**
 * Type used for constructing a dictionary (hash map), which is in turn used to locate existing shared memory segments.
 */
typedef struct shm_dictionary_entry {
    /* For now, the ID is the concatenation of sender and the receiver ID */
    char *id;
    /* Used by uthash for internal bookkeeping; must be present. */
    UT_hash_handle hh;
} shm_dict_entry;

/**
 * Dictionary (hash map) used for looking up if there is an existing shared memory segment for two processes wishing to communicate.
 */
shm_dict_entry *shm_dict = NULL;



void send(msg* m) {
    shm_dict_entry *entry;
    // construct the key by concatenating sender and receiver IDs; TODO: faster/better way to do this?
    int str_len = nDigits(m->senderId) + nDigits(m->rcvrId);
    char identifier[str_len];
    sprintf(identifier, "%d%d", m->senderId, m->rcvrId);
    // TODO: need to account for str_len when doing malloc for struct?
    entry = (shm_dict_entry*) malloc(sizeof *entry);
    HASH_FIND_STR(shm_dict, identifier, entry);
    if(entry == NULL) {
        printf("no entry found for '%s'\n", identifier);
        /* TODO create shared memory segment and insert in map */
    }
    /* TODO shared memory segment already already present, use it (access it using 'entry') */
    printf("send(msg* m) invoked; m->senderId=%d m->rcvrId=%d m->payload='%s'\n", m->senderId, m->rcvrId, m->payload);
    printf("callers pid=%d\n", getpid());
    printf("cached pid=%d\n", senderId);
}

msg* constructMsg(char *content, int receiverId) {
    msg* message;
    // TODO / Q: how to account for variable length of *content when doing malloc?
    message = (msg*) malloc(sizeof(msg));
    if (message == NULL) {
        printf("out of memory\n");
        // TODO how to handle?
    }
    // Perform syscall to get sender id if not already cached.
    if (senderId < 0) {
        // Assumption: all pids are non negative.
        senderId = getpid();
        printf("updated cached pid to=%d\n", senderId);
    }
    message->senderId = senderId;
    message->rcvrId = receiverId;
    // TODO consider copying for safety; but bad for perforamnce :-(.
    message->payload = content;
    return message;
}
