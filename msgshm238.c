#include <stdio.h>
#include <stdlib.h>
#include "msgshm238.h"
#include <unistd.h>
#include <sys/types.h>

/**
 * Manually cache the pid to avoid a system call for each send.
 * Apparently the library did provide automatic caching of the pid in some versions, but this caused bugs.
 * See http://man7.org/linux/man-pages/man2/getpid.2.html
 */
pid_t senderId = -42;

void send(msg* m) {
    printf("send(msg* m) invoked; m->senderId=%d m->rcvrId=%d m->payload='%s'\n", m->senderId, m->rcvrId, m->payload);
    printf("callers pid=%d\n", getpid());
//    if (senderId < 0) {
//        // Assumption: all pids are non negative.
//        senderId = getpid();
//        printf("updated cached pid to=%d\n", senderId);
//    }
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
