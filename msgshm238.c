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
#include <stdatomic.h>

/**
 * Max number of chars needed to convert an int to a string.
 * See https://stackoverflow.com/a/10536254/1214974
 */
#define INT_AS_STR_MAX_CHARS 3*sizeof(int)+2


//#define SHM_SEGMENT_UNLOCKED -1

/**
 * The maximum number of messages that can be in the message buffer (shared memory segment) at any time.
 * TODO update to proper size -- for now allow room for two messages.
 */
#define BUFFER_MSG_CAPACITY 2

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
 * Value of shm_header.pIdOfCurrent when shared memory segment is unlocked.
 */
const pid_t SHM_SEGMENT_UNLOCKED = -1;

pid_t get_sender_pid() {
    // Perform syscall to get sender id if not already cached.
    if (senderId < 0) {
        // Assumption: all pids are non negative.
        senderId = getpid();
        printf("updated cached pid to=%d\n", senderId);
    }
    return senderId;
}

/**
 * Type used for constructing a dictionary (hash map), which is in turn used to locate existing shared memory segments.
 */
typedef struct shm_dictionary_entry {
    /**
     * For now, the ID is the concatenation of sender and the receiver ID, hence we must allow room for two ints.
     * In addition, we must prepend the concatenation of the two IDs with a slash as per the convention specified in the man pages (hence we need the +1 to allow space for an extra char).
     * See http://man7.org/linux/man-pages/man3/shm_open.3.html
     */
//    char id[2*INT_AS_STR_MAX_CHARS+1];
    char *id;
    /**
     * Pointer to start of shared memory segment.
     */
    void *addr;
    /* Used by uthash for internal bookkeeping; must be present. */
    UT_hash_handle hh;
} shm_dict_entry;

/**
 * Dictionary (hash map) used for looking up if there is an existing shared memory segment for two processes wishing to communicate.
 */
shm_dict_entry *shm_dict = NULL;


//    int res = atomic_compare_exchange_weak(&(int *)addr, 0, senderId);


int put_msg(shm_dict_entry * shm_ptr, int rcvrId, char * payload) {
    int expected;
    // Read the header which resides at the front of the shared memory segment.
    shm_header * header = (shm_header *)shm_ptr->addr;
    // Refresh cache with sender's pid if necessary.
    get_sender_pid();
    // Spin lock -- wait for exclusive access.
    expected = SHM_SEGMENT_UNLOCKED;
    while(!atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, senderId)) {
        // Unfortunately have to make it this verbose since expected is overwritten if the result is false.
        // See last answer here.
        // https://stackoverflow.com/questions/16043723/why-do-c11-cas-operations-take-two-pointer-parameters
        expected = SHM_SEGMENT_UNLOCKED;
    }

    if (header->msg_count == BUFFER_MSG_CAPACITY) {
        // Buffer is full.
        printf("buffer is full, cannot add msg\n");
        // Release lock and return error code.
        expected = senderId;
        while(atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, SHM_SEGMENT_UNLOCKED)) {
            expected = senderId;
        }
        return -1;
    }
    /*
     * Offset of the new message in shared memory segment is the byte immediately after the most
     * recently added message, i.e., its starting position is the header size plus the size of a
     * message times the number of messages currently in the segment.
     * However, note that we wrap around if we reached end of buffer but have space available at
     * the front because one of the earlier elements have been read.
     *
     * TODO: need +1 byte? Or is this taken care of by 0-indexed approach?
     */
    size_t msg_offset = sizeof(shm_header) + sizeof(msg) * (header->msg_count % BUFFER_MSG_CAPACITY);
//    msg * new_msg = (msg*) shm_ptr->addr + sizeof(shm_header) + sizeof(msg) * header->msg_count;
    msg * new_msg = (msg*) shm_ptr->addr + msg_offset;
    new_msg->senderId = senderId;
    new_msg->rcvrId = rcvrId;
    size_t payload_len = strlen(payload); // Assumes null-terminated string?
    if (payload_len + 1 > MAX_PAYLOAD_SIZE) {
        /* +1 to account for null-terminating char */
        /*
         * TODO payload too large to fit in one message.
         * For now truncate it.
         * Later on we may want to experiment with breaking it into multiple messages.
         * Note: -1 in order to make room for null terminator.
         */
        strncpy(new_msg->payload, payload, MAX_PAYLOAD_SIZE - 1);
        // Insert the null-terminator.
        new_msg->payload[MAX_PAYLOAD_SIZE - 1] = '\0';
    } else {
        strncpy(new_msg->payload, payload, MAX_PAYLOAD_SIZE);
    }
    // ------------------------------------------------------------------------------------
    // Update header to reflect the change.
    header->msg_count++;
    /*
     * Update the index of the newest message. Note that we wrap around if we reached end of buffer
     * but have space available at the front because one of the earlier elements have been read.
     */
    header->newest = (header->newest + 1) % BUFFER_MSG_CAPACITY;
    if (header->msg_count == 1) {
        // If the buffer was empty, this new message is both the newest and the oldest.
        header->oldest = header->newest;
    }
    // ------------------------------------------------------------------------------------

    // Unlock.
    // Note that loop is necessary even though we already hold the lock as the _weak version is allowed to fail spuriously (see doc).
    expected = senderId;
    while(!atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, SHM_SEGMENT_UNLOCKED)) {
        expected = senderId;
    }
    printf("successfully added message with payload '%s' to buffer\n", payload);
    // 0 for success.
    return 0;
}

void init_shm_header(shm_dict_entry * shm_ptr) {
    // TODO should encapsulate this initialization in inter process mutexes to avoid two processes initializing the header concurrently.
    // The header is placed at the front of the shared memory segment.
    shm_header * header = (shm_header *)shm_ptr->addr;
    header->msg_count = 0;
    // -1 to indicate no messages. Note that this also ensures that we start at 0 for the first message when incrementing header->newest in put_msg.
    header->newest = -1;
    header->oldest = -1;
    // TODO: set self as current accessor immediately?
    // If we do so, we will need to update compare and swap check in put_msg function.
    header->pIdOfCurrent = SHM_SEGMENT_UNLOCKED;
}

///**
// * Use factory pattern in order to ensure that shm_dict_entry is fully built or not built at all.
// * Courtesy of https://stackoverflow.com/a/14768280/1214974
// */
//shm_dict_entry * new_shm_dict_entry(char * id, void * addr) {
//    shm_dict_entry entry = malloc(sizeof(shm_dict_entry));
//    if (entry == NULL) {
//        // out of memory
//        return NULL;
//    }
//    // No need to malloc for values pointed to by id and addr as the caller has already performed those mallocs.
//    entry->id = id;
//    entry->addr = addr;
//}

void send(char * payload, int receiverId) {
    // File descriptor for the shared memory segment.
    int fd;
    // Identifier for the shared memory segment we are looking up.
    char *identifier = malloc(2*INT_AS_STR_MAX_CHARS+1);
    // Entry in map for memory segment.
    shm_dict_entry *entry = malloc(sizeof(*entry));
    // -------------------------------------------------------------------------

    senderId = get_sender_pid();
    printf("send(char *, int) invoked by caller with pid=%d; rcvrId=%d; payload='%s'\n", senderId, receiverId, payload);

    printf("cached pid=%d\n", senderId);
    // construct the key by concatenating sender and receiver IDs
    sprintf(identifier, "/%d%d", senderId, receiverId); // TODO add delimiter for uniqueness
    // Check if there's already an entry (and thereby already an open shared memory segment).
    HASH_FIND_STR(shm_dict, identifier, entry);
    if(entry == NULL) {
        printf("No entry found for '%s'; creating new shared memory segment...\n", identifier);
        // Create and open new shared memory segment.
        fd = shm_open(identifier, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            printf("Error creating new shared memory segment.\n");
            // TODO: respond to error.
            return; // TODO return error code
        } else {
            void *addr;
            printf("Successfully created new shared memory segment with fd=%d\n", fd);
            // Apparently need to reallocate here in order to avoid segmentation fault. Q: where to free what we allocated earlier?
            free(entry);
            // Init shm_dict_entry for created shm segment.
            entry = malloc(sizeof(shm_dict_entry));
            if (entry == NULL) {
                // TODO error handling.
                printf("send(char *, int) out of memory when re-allocating memory for shm_dict_entry\n");
                // TODO free payload or leave that to caller?
                return;
            }
            /*
             * New shared memory segments have length 0, so need to size it.
             * Essentially the size chosen here will be the size of our message queue/buffer.
             * We've made it hard for ourselves by choosing a variable size content length of messages (the payload pointer).
             */
            size_t shm_segment_size = sizeof(shm_header) + sizeof(msg) * BUFFER_MSG_CAPACITY;
            ftruncate(fd, shm_segment_size);
            // Map shared memory segment into own address space.
            addr = mmap(NULL, shm_segment_size , PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            // Apparently fd is no longer needed. We can later unlink the shared memory segment using the identifer.
            close(fd);
            // Set fields in new entry and update map with new entry.
            entry->id = identifier;
            entry->addr = addr;
            // Note that the second parameter (in this case 'id') is the name of the shm_dict_entry field that should be used as key.
            HASH_ADD_STR(shm_dict, id, entry);
            // Setup shm segment header.
            init_shm_header(entry);
        }
    }
    // Reinit entry in case it was null above -- TODO necessary as we init entry during the if block?
    HASH_FIND_STR(shm_dict, identifier, entry);
    put_msg(entry, receiverId, payload);
}

msg *recv(int senderId){

    char *identifier = malloc(2*INT_AS_STR_MAX_CHARS+1);
    int receiverId = getpid();

    sprintf(identifier, "/%d%d", senderId, receiverId);

    shm_dict_entry *entry = malloc(sizeof(*entry));
    HASH_FIND_STR(shm_dict, identifier, entry);

     int ShmID = shmget(identifier, sizeof(shm_header) + sizeof(char *) * BUFFER_MSG_CAPACITY, SHM_R|SHM_W);
     if (ShmID < 0) {
          printf("*** shmget error (client) ***\n");
          exit(1);
     }

//    if(entry == NULL) {
//        // shared memory does not exist
//        printf("the shared memory does not exist");
//        return NULL;
//    }
//    else{
//        // read from the shared memory
//        printf("shared memory found");
//    }



}

//msg recv(int senderId) {
//    // 1 locate shm segment
//    // 2 extract message, if any
//
//
//}


// ---------------------------------------------------------------
// Begin old pointer-based approach

//void put_msg(msg * m, shm_dict_entry * shm_ptr) {
//    // Spin lock -- wait for exclusive access.
//    while(!atomic_compare_exchange_weak(/* get value of shm_segment_header->pidOfCurrentAccesor */, 0, senderId));
//
//    // Read the header which resides at the front of the shared memory segment.
//    shm_header = &(shm_header *)shm_ptr->addr; // TODO fix this.
//
//    start =  == NULL ? shm_ptr
//
//    memcpy(start, m /* plus its payload - how? */, sizeof(m /* plus its payload - how? */));
//
//
//    // Unlock.
//    // Note that loop is necessary even though we already hold the lock as the _weak version is allowed to fail spuriously (see doc).
//    while(atomic_compare_exchange_weak(/* get value of shm_segment_header->pidOfCurrentAccesor */, senderId, 0));
//}

//void send(msg* m) {
//    // ---- ANSI C requires variables to be declared at the start of a block ---
//    // File descriptor for the shared memory segment.
//    int fd;
//    // Identifier for the shared memory segment we are looking up.
//    char *identifier = malloc(2*INT_AS_STR_MAX_CHARS+1);
//    // Entry in map for memory segment.
//    shm_dict_entry *entry = malloc(sizeof(*entry));
//    // -------------------------------------------------------------------------
//    printf("send(msg* m) invoked by caller with pid=%d; m->senderId=%d m->rcvrId=%d m->payload='%s'\n", getpid(), m->senderId, m->rcvrId, m->payload);
//    printf("cached pid=%d\n", senderId);
//    // construct the key by concatenating sender and receiver IDs
//    sprintf(identifier, "/%d%d", m->senderId, m->rcvrId);
//    // Check if there's already an entry (and thereby already an open shared memory segment).
//    HASH_FIND_STR(shm_dict, identifier, entry);
//    if(entry == NULL) {
//        printf("No entry found for '%s'; creating new shared memory segment...\n", identifier);
//        // Create and open new shared memory segment.
//        fd = shm_open(identifier, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
//        if (fd == -1) {
//            printf("Error creating new shared memory segment.\n");
//            // TODO: respond to error.
//            return; // TODO return error code
//        } else {
//            void *addr;
//            // Apparently need to reallocate here in order to avoid segmentation fault. Q: where to free what we allocated earlier?
//            free(entry);
//            entry = malloc(sizeof(shm_dict_entry));
//            printf("Successfully created new shared memory segment with fd=%d\n", fd);
//            /*
//             * New shared memory segments have length 0, so need to size it.
//             *
//             * TODO: need to figure out a way to come up with a reasonable size for the memory segment; for now just size it according to the incoming message.
//             * Essentially the size chosen here will be the size of our message queue/buffer.
//             * We've made it hard for ourselves by choosing a variable size content length of messages (the payload pointer).
//             * Maybe consider making payload a fixed size and null terminate it if the message payload is shorter than what we leave space for.
//             * Ideas?
//             */
//            ftruncate(fd, sizeof(m));
//            // Map shared memory segment into own address space.
//            addr = mmap(NULL, sizeof(m), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
//            // Apparently fd is no longer needed. We can later unlink the shared memory segment using the identifer.
//            close(fd);
//            // Update map with new entry...
//            // Must allocate size for the shared memory segment identifier separetely as it is a pointer.
//            entry->id = malloc(sizeof(*identifier)); // Q malloc needed here? Runs fine w/o it.
//            entry->id = identifier;
//            entry->addr = malloc(sizeof(*addr)); // Q malloc needed here? Runs fine w/o it.
//            entry->addr = addr;
//            HASH_ADD_STR(shm_dict, id, entry);
//            // Setup shm segment header
//            init_shm_header(entry);
//        }
//    }
//    // Reinit entry in case it was null above -- TODO necessary as we init entry during the if block?
//    HASH_FIND_STR(shm_dict, id, entry);
//    put_msg(m, entry);
//}

//msg* constructMsg(char *content, int receiverId) {
//    msg* message;
//    // TODO / Q: how to account for variable length of *content when doing malloc?
//    message = (msg*) malloc(sizeof(msg));
//    if (message == NULL) {
//        printf("out of memory\n");
//        // TODO how to handle?
//    }
//    // Perform syscall to get sender id if not already cached.
//    if (senderId < 0) {
//        // Assumption: all pids are non negative.
//        senderId = getpid();
//        printf("updated cached pid to=%d\n", senderId);
//    }
//    message->senderId = senderId;
//    message->rcvrId = receiverId;
//    // TODO consider copying for safety; but bad for perforamnce :-(.
//    message->payload = content;
//    return message;
//}

// End old pointer-based approach
// ---------------------------------------------------------------
