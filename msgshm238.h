#include <semaphore.h>

sem_t mutex;

// TODO update to proper size, for now keep low so that we can experiment with breaking long payloads into multiple messages
#define MAX_PAYLOAD_SIZE 32

// ---------------------------------------------------------------
// Begin old pointer-based approach

//struct msgTag;

//// Encapsulates the information necessary to send a message to a process.
//typedef struct msgTag {
//
//    // message header that includes msg ID, sender, and receiver information
//    int msgHeader;
//    //sender's (process) ID
//    int senderId;
//    // (Proccess) ID of receiver. TODO: is it more efficient to use pointer to avoid copying (pass-by-value)?
//    int rcvrId;
//    // ... TODO pointer to location (or copy) of data to be shared.
//    // use of pointer may be more efficient as only one copy will be necessary (from private memory to shared memory segment)
//    // in contrast, pass-by-value will require a copy from local memory to local variable and then to shared memory segment.
//
//    //message itself....not sure about data type...for now using string
//    char * payload;
//    // next
//} msg;

//// Receive a message from a sender with (process) ID == senderId.
//// Returns a pointer to the message to avoid overhead caused by copying imposed by pass-by-value.
//// TODO: Might actually be necessary to copy msg to local memory for safety purposes (as sender could potentially modify the shared memory segment later on)
//msg* recv(int senderId);

//// Send message m
//void send(msg* m);

// construct a message bundle
//msg *constructMsg(char *msg, int receiverId);

//// function that serializes complex structure to charcter array
//char *serializeMsg(msg);

// End old pointer-based approach
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// Begin new copy- and fixed size-based approach

typedef struct msgTag {
    
    // message header that includes msg ID, sender, and receiver information
    int msgHeader;
    //sender's (process) ID
    int senderId;
    // (Proccess) ID of receiver. TODO: is it more efficient to use pointer to avoid copying (pass-by-value)?
    int rcvrId;
    // ... TODO pointer to location (or copy) of data to be shared.
    // use of pointer may be more efficient as only one copy will be necessary (from private memory to shared memory segment)
    // in contrast, pass-by-value will require a copy from local memory to local variable and then to shared memory segment.
    
    //message itself....not sure about data type...for now using string
    char payload[MAX_PAYLOAD_SIZE];
    // next
} msg;


/**
 * Send message with contents pointed to by 'payload' to process with pid 'receiverId'.
 */
void send(char * payload, int receiverId);

/**
 * Read (receive) a message sent by process with pid 'senderId'.
 */
msg recv(int senderId);

// End new copy- and fixed size-based approach
// ---------------------------------------------------------------



// ---------------------------------------------------------------
// Begin notes

//----------------------------------------------------------------------------------
// Brainstorm -> idea for how to implement blocking recv:
// Sender (on start up or at first send) locks some memory location identified by its process ID;
// does not release lock until after it has stored some message in shared memory;
// then releases lock as a way of notifying receiver that data is ready.
// ...
// ... needs more thought
//----------------------------------------------------------------------------------



// data structure in shared memory segment
// [ {messagesNum, head, tail, pIdOfCurrentAccessor} msg, msg, msg, msg, msg  ...  ]


//    shm_ptr->addr->tail->next = memory
//    shm_ptr->addr->tail = shm_ptr->addr->tail->next
//    [ shm_header {mem_block_head} {men_block_tail}    ]

// End notes
// ---------------------------------------------------------------
