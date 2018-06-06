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
#include <sys/ipc.h>

/**
 * Max number of chars needed to convert an int to a string.
 * See https://stackoverflow.com/a/10536254/1214974
 */
#define INT_AS_STR_MAX_CHARS 3*sizeof(int)+2

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
pid_t invoker_pid = -42;

/**
 * Value of shm_header.pIdOfCurrent when shared memory segment is unlocked.
 */
const pid_t SHM_SEGMENT_UNLOCKED = -1;

pid_t get_invoker_pid() {
    // Perform syscall to get sender id if not already cached.
    if (invoker_pid < 0) {
        // Assumption: all pids are non negative.
        invoker_pid = getpid();
        printf("updated cached pid to=%d\n", invoker_pid);
    }
    return invoker_pid;
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

/**
 * Utility function for initializing a pointer that points to a value that is the identifier of the shared
 * memory segment that processes with pid1 and pid2 use to communicate. By setting the value here, we make
 * sure that the shared memory id naming scheme can be changed in a single place (in case we want to change
 * the naming scheme later on). Note that it is up to the caller to free the returned pointer.
 *
 * Current naming scheme: the concatenation of the two pids separated by a delimiter (an underscore); the
 * lowest pid goes first (this retains consistency for the id, regardless of this function being
 * invoked from the send or recv function).
 */
char * get_shm_id_for_processes(int pid1, int pid2) {
    // +1 for slash and +1 for delimiter (underscore)
    char * shm_id = malloc(2*INT_AS_STR_MAX_CHARS+1+1);
    if (pid1 < pid2) {
        sprintf(shm_id, "/%d_%d", pid1, pid2);
    } else {
        sprintf(shm_id, "/%d_%d", pid2, pid1);
    }
    return shm_id;
}

/**
 * Look up the hash table, searching for a shared memory segment for processes with pids pid1 and pid2.
 * The caller is responsible for freeing the shm_dict_entry once it is no longer needed. Note that such
 * a free call will affect the hash table!
 */
shm_dict_entry* find_shm_dict_entry_for_shm_segment(int pid1, int pid2) {
    // Identifier for shm segment.
    char * shm_id = get_shm_id_for_processes(pid1, pid2);
    // Dictionary entry for the shm segment.
    shm_dict_entry *entry = NULL;
    // Lookup dict entry.
    HASH_FIND_STR(shm_dict, shm_id, entry);
    // Clean up
    free(shm_id);
    // Note: NULL is returned if no match.
    return entry;
}

int put_msg(shm_dict_entry * shm_ptr, int rcvrId, char * payload) {
    int expected;
    // Read the header which resides at the front of the shared memory segment.
    shm_header * header = (shm_header *)shm_ptr->addr;
    // Refresh cache with sender's pid if necessary.
    get_invoker_pid();
    // Spin lock -- wait for exclusive access.
    expected = SHM_SEGMENT_UNLOCKED;
    while(!atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, invoker_pid)) {
        // Unfortunately have to make it this verbose since expected is overwritten if the result is false.
        // See last answer here.
        // https://stackoverflow.com/questions/16043723/why-do-c11-cas-operations-take-two-pointer-parameters
        expected = SHM_SEGMENT_UNLOCKED;
    }

    if (header->msg_count == BUFFER_MSG_CAPACITY) {
        // Buffer is full.
        printf("buffer is full, cannot add msg\n");
        // Release lock and return error code.
        expected = invoker_pid;
        while(atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, SHM_SEGMENT_UNLOCKED)) {
            expected = invoker_pid;
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
    msg * new_msg = (msg*) shm_ptr->addr + msg_offset;
    new_msg->senderId = invoker_pid;
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
    expected = invoker_pid;
    while(!atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, SHM_SEGMENT_UNLOCKED)) {
        expected = invoker_pid;
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

int create_shared_mem_segment(int pid1, int pid2) {
    // File descriptor for the shared memory segment.
    int fd;
    // Construct identifier for new shared mem segment.
    char * identifier = get_shm_id_for_processes(pid1, pid2);
    printf("Creating new shared memory segment with id=%s...\n", identifier);
    // Create and open new shared memory segment.
    fd = shm_open(identifier, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    /*
     * TODO CHECK HERE IF ERROR CODE MEANS SEGMENT ALREADY EXISTED.
     * IF THIS IS THE CASE, IT MEANS THAT THE OTHER PROCESS CREATED
     * SHARED MEMORY SEGMENT BEFORE US, AND WE SHOULD SIMPLY ATTACH
     * TO IT.
     */
    if (fd == -1) {
        printf("Error creating new shared memory segment.\n");
        // Return error code letting caller now that segment could not be created.
        return -1;
    } else {
        // Pointer to starting location of new shared memory segment.
        void *addr;
        printf("Successfully created new shared memory segment with fd=%d\n", fd);
        // Init shm_dict_entry for created shm segment.
        shm_dict_entry* entry = malloc(sizeof(shm_dict_entry));
        if (entry == NULL) {
            // Out of memory. Return error code to let caller know.
            // TODO should close the shared memory segment here...
            return -2;
        }
        /*
         * New shared memory segments have length 0, so need to size it.
         * The size chosen here will be the size of our message queue/buffer.
         * Allow room for the header and a fixed number of messages.
         */
        size_t shm_segment_size = sizeof(shm_header) + sizeof(msg) * BUFFER_MSG_CAPACITY;
        ftruncate(fd, shm_segment_size);
        // Map shared memory segment into own address space.
        addr = mmap(NULL, shm_segment_size , PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        // fd is no longer needed - we can unlink the shared memory segment using the identifer.
        close(fd);
        // Set fields in new entry and update map with new entry.
        entry->id = identifier;
        entry->addr = addr;
        // The second parameter ('id') is the name of the shm_dict_entry field that should be used as key.
        HASH_ADD_STR(shm_dict, id, entry);
        // Setup shm segment header.
        init_shm_header(entry);
        // Return 0 to indicate success.
        return 0;
    }
}

void send(char * payload, int receiverId) {
    // Refresh cached pid if needed.
    invoker_pid = get_invoker_pid();
    printf("send(char *, int) invoked by caller with pid=%d; rcvrId=%d; payload='%s'\n", invoker_pid, receiverId, payload);
    // Locate the shared memory segment if one already exists by querying the hash table.
    shm_dict_entry *entry = find_shm_dict_entry_for_shm_segment(invoker_pid, receiverId);
    if(entry == NULL) {
        // No **knowledge of** existing shared memory segment, so set one up.
        // Note that a shared mem segment may in fact exist, we just haven't interacted with it yet.
        // It is up to create_shared_mem_segment to attach to it (instead of creating it) if it already exists.
        int created = create_shared_mem_segment(invoker_pid, receiverId);
        if (0 != created) {
            // TODO error handling
            printf("[ERROR] create_shared_mem_segment(int,int) returned error code %d. Memory segment not created.\n", created);
            return;
        }
        // As the shm segment has now been created, there should now be a corresponding entry in the map.
        // Fetch it as we need it for put_msg below.
        entry = find_shm_dict_entry_for_shm_segment(invoker_pid, receiverId);
    }
    // Finally place the message in the shared memory segment.
    put_msg(entry, receiverId, payload);
}

msg* fetch_msg(shm_dict_entry* shm_ptr, int senderId) {
    int expected;
    // Read the header which resides at the front of the shared memory segment.
    shm_header * header = (shm_header *)shm_ptr->addr;
    // Spin lock -- wait for exclusive access.
    expected = SHM_SEGMENT_UNLOCKED;
    while(!atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, invoker_pid)) {
        expected = SHM_SEGMENT_UNLOCKED;
    }
    // Exclusive access to shm segment obained.
    
    /*
     * Calcuate the offset of the oldest message in the buffer.
     *
     * TODO: the oldest message might be one sent by invoker_pid itself, so may
     * need to advance value pointed to by oldest by +1 until we get to a message
     * where msg->senderId != invoker_pid.
     */
    size_t msg_offset = sizeof(shm_header) + sizeof(msg) * (header->oldest);
    msg* shared_msg = (msg*) shm_ptr->addr + msg_offset;
    // Make a copy of the message in local memory for safety and to free space
    // in the shared memory segment.
    msg* local_msg = malloc(sizeof(*local_msg));
    if (local_msg == NULL) {
        /* Ugh, out of memory */
        printf("[ERROR] could not malloc memory for local_msg when copying from shared_msg read from shared memory\n");
        return NULL;
    }
    local_msg->senderId = shared_msg->senderId; // Need to copy?
    local_msg->rcvrId = shared_msg->rcvrId; // Need to copy?
    // Copy the payload to the local msg.
    strncpy(local_msg->payload, shared_msg->payload, MAX_PAYLOAD_SIZE);
    
    /*
     * Set flag that indicates that shared_msg was read and is no longer needed.
     *
     * TODO for now we can just increment shm_header->oldest, but this WON'T WORK
     * if shm_header->oldest is a message sent by invoker_pid and we therefore
     * 'proceed' more recent messages (see note above).
     */
    header->oldest = header->oldest + 1 % BUFFER_MSG_CAPACITY;
    
    
    
    // Unlock exclusive access to shared memory segment.
    expected = invoker_pid;
    while(!atomic_compare_exchange_weak(&(header->pIdOfCurrent), &expected, SHM_SEGMENT_UNLOCKED)) {
        expected = invoker_pid;
    }
    return local_msg;
}

msg* recv(int senderId) {
    // Refresh cached pid if needed.
    get_invoker_pid();
    printf("recv(int senderId) invoked by caller with pid=%d; senderId=%d\n", invoker_pid, senderId);
    // Locate the shared memory segment if one already exists by querying the hash table.
    shm_dict_entry *entry = find_shm_dict_entry_for_shm_segment(invoker_pid, senderId);
    if (entry == NULL) {
        // No **knowledge of** existing shared memory segment, so set one up.
        // Note that a shared mem segment may in fact exist, we just haven't interacted with it yet.
        // It is up to create_shared_mem_segment to attach to it (instead of creating it) if it already exists.
        int created = create_shared_mem_segment(invoker_pid, senderId);
        if (0 != created) {
            // TODO error handling
            printf("[ERROR] create_shared_mem_segment(int,int) returned error code %d. Memory segment not created.\n", created);
            return NULL;
        }
        // As the shm segment has now been created, there should now be a corresponding entry in the map.
        // Fetch it as we need it for fetch_msg below.
        entry = find_shm_dict_entry_for_shm_segment(invoker_pid, senderId);
    }
    msg* m = fetch_msg(entry, senderId);
    return m;
}

/* maruf recv
msg *recv(int senderId){
    
    char *identifier = malloc(2*INT_AS_STR_MAX_CHARS+1);
    // Get own pid from cache or update cache if pid not previously read.
    int receiverId = get_invoker_pid();

    sprintf(identifier, "/%d%d", senderId, receiverId);

    shm_dict_entry *entry = malloc(sizeof(*entry));
    HASH_FIND_STR(shm_dict, identifier, entry);

    // If a shared memory segment already exists with this key, return it; otherwise create a new one for this key and return that.
     int ShmID = shmget(identifier, sizeof(shm_header) + sizeof(msg) * BUFFER_MSG_CAPACITY, IPC_CREAT);
     if (ShmID >0) {
          printf("shared memory exist and found \n");
          char *data;

          data = shmat(ShmID, (void *)0, 0);
          if (data == NULL)
                perror("shmat");
          else
              printf("%s",data);

     }
     else{
          printf("shared memory does not exist\n");
          //MAYBE WE ALLOCATE NEW MEMORY BLOCK?

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
*/
