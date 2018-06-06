#include <stdio.h>
//#include <mpi.h>
#include <stdlib.h>
#include "msgshm238.h"

void strings() {
    char str[] = "no notion of 'wrapper' String class/struct like in java;\nstrings are declared as char arrays.\n";
    printf("%s", str);
}

int main(void) {
    if (-42) {
//        printf("negative numbers evaluate to true\n");
    }
//    msg* someMsg = constructMsg("hello", 42);
//    send(someMsg);
//    send(someMsg);
    char * str1 = "string1";
    char * str2 = "string2";
    send(str1, 42);
    send(str2, 42);
    return 0;
}


