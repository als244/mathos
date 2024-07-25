CC = gcc
CFLAGS = -g -std=c99 -Wall -pedantic 

all: testMaster testWorker

## executables


## MASTER PROGRAM
testMaster: main_master.c master.o self_net.o table.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread -libverbs

master.o: master.c
	${CC} ${CFLAGS} -c $^


## WORKER PROGRAM

testWorker: main_worker.c self_net.o join_net.o table.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread -libverbs 


join_net.o: join_net.c
	${CC} ${CFLAGS} -c $^


## BOTH

self_net.o: self_net.c
	${CC} ${CFLAGS} -c $^

table.o: table.c
	${CC} ${CFLAGS} -c $^


clean:
	rm -f *.o testMaster testWorker
