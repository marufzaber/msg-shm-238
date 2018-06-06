#include <stdio.h>
//#include <mpi.h>
#include <stdlib.h>
#include <unistd.h>
#include "msgshm238.h"

void strings() {
    char str[] = "no notion of 'wrapper' String class/struct like in java;\nstrings are declared as char arrays.\n";
    printf("%s", str);
}

int main(void) {

    char * str1 = "string1";
    char * str2 = "string2";
    send(str1, 42);
    send(str2, 42);
    /*
    printf("PID: %d Running\n",getpid());
    printf("enter s to send and r to receive:");
    char ch;
    scanf("%c",&ch);
    if(ch == 's'){
        int receiver_id;
        printf("enter receiver id : ");
        scanf("%d",&receiver_id);
        send(str1, receiver_id);
        while(1);
    }
    else if(ch == 'r'){
        int sender_id;
        printf("enter sender id : ");
        scanf("%d",&sender_id);
        recv(sender_id);
        //while(1);
    }
    */
    return 0;
}


