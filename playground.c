#include <stdio.h>
#include <mpi.h>
#include <stdlib.h>



void strings() {
    char str[] = "no notion of 'wrapper' String class/struct like in java;\nstrings are declared as char arrays.\n";
    printf("%s", str);
}

int main(void) {
    printf("Hello World!\n");
    if (-42) {
        printf("negative numbers evaluate to true\n");
    }
    strings();
    return 0;
}


