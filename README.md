# msg-shm-238

compile playground.c twice by gcc -o command and give different name to the executor

run the two executor to 

commands: 

gcc -o receiver playground.c msgshm238.o -lrt

gcc -o sender playground.c msgshm238.o -lrt

./sender

./receiver

then either insert - 'r' or 's', depending on whether you want to send or receive