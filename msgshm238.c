#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msgshm238.h"
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h> /* O_* constnats */
#include <sys/stat.h> /* mode constants */
#include <sys/mman.h>
#include "uthash.h" /* Hash table provided by https://troydhanson.github.io/uthash/ */

/**
 * Max number of chars needed to convert an int to a string.
 * See https://stackoverflow.com/a/10536254/1214974
 */
#define INT_AS_STR_MAX_CHARS 3*sizeof(int)+2

/*
 * [Note to self]
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
    /**
     * For now, the ID is the concatenation of sender and the receiver ID, hence we must allow room for two ints.
     * In addition, we must prepend the concatenation of the two IDs with a slash as per the convention specified in the man pages (hence we need the +1 to allow space for an extra char).
     * See http://man7.org/linux/man-pages/man3/shm_open.3.html
     */
    char id[2*INT_AS_STR_MAX_CHARS+1];
    /**
     * Poiner to start of shared memory segment.
     */
    char *addr;
    /* Used by uthash for internal bookkeeping; must be present. */
    UT_hash_handle hh;
} shm_dict_entry;

/**
 * Dictionary (hash map) used for looking up if there is an existing shared memory segment for two processes wishing to communicate.
 */
shm_dict_entry *shm_dict = NULL;



void send(msg* m) {
    printf("send(msg* m) invoked by caller with pid=%d; m->senderId=%d m->rcvrId=%d m->payload='%s'\n", getpid(), m->senderId, m->rcvrId, m->payload);
    printf("cached pid=%d\n", senderId);
    
    shm_dict_entry *entry;
    // construct the key by concatenating sender and receiver IDs
    char identifier[INT_AS_STR_MAX_CHARS];
    sprintf(identifier, "/%d%d", m->senderId, m->rcvrId);
    entry = (shm_dict_entry*) malloc(sizeof *entry);
    // File descriptor for the shared memory segment.
    int fd;
    // Check if there's already an entry (and thereby already an open shared memory segment).
    HASH_FIND_STR(shm_dict, identifier, entry);
    if(entry == NULL) {
        printf("No entry found for '%s'; creating new shared memory segment...\n", identifier);
        /* TODO create shared memory segment and insert in map */
        fd = shm_open(identifier, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            printf("Error creating new shared memory segment.\n");
        } else {
            printf("Successfully created new shared memory segment with fd=%d\n", fd);
            /*
             * New shared memory segments have length 0, so need to size it.
             *
             * TODO: need to figure out a way to come up with a reasonable size for the memory segment; for now just size it according to the incoming message.
             * Essentially the size chosen here will be the size of our message queue/buffer.
             * We've made it hard for ourselves by choosing a variable size content length of messages (the payload pointer).
             * Maybe consider making payload a fixed size and null terminate it if the message payload is shorter than what we leave space for.
             * Ideas?
             */
            ftruncate(fd, sizeof(m));
            // Map shared memory segment into own address space.
            char *addr;
            printf("before mmap\n");
            addr = mmap(NULL, sizeof(m), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            // Apparently fd is no longer needed. We unlink using the identifer.
            printf("before close\n");
            close(fd);
            // Update map with new entry.
            printf("before strncpy\n");
            strncpy(entry->id, identifier, 2*INT_AS_STR_MAX_CHARS+1);
            printf("after strncpy\n");
            entry->addr = addr;
//            *entry = { .id = identifier, .addr = addr };
//            shm_dict_entry dummy = { .id = identifier, .addr = addr };
            printf("before adding to hashmap\n");
            HASH_ADD_STR(shm_dict, id, entry);
        }
    } else {
        /* TODO shared memory segment already already present, use it (access it using 'entry') */
        printf("Found entry in hash map for '%s'\n", identifier);
    }
    
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
