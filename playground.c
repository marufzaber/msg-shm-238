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

    printf("PID: %d Running\n",getpid());
    printf("enter s to send and r to receive:");
    char ch;
    scanf("%c",&ch);
    if(ch == 's')
        send(str1, 42);
    else if(ch == 'r'){
        int sender_id;
        printf("enter sender id : ");
        scanf("%d",&sender_id);
        recv(sender_id);
    }
    return 0;
}


