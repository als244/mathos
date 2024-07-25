CC = gcc
CFLAGS = -g -std=c99 -Wall -pedantic 

all: testMaster testSelfNet

## master process
testMaster: main_master.c master.o table.o self_net.o
	${CC} ${CFLAGS} $^ -o $@ -lm -libverbs

## worker process
testSelfNet: main_worker.c self_net.o
	${CC} ${CFLAGS} $^ -o $@ -libverbs


## both
table.o: table.c
	${CC} ${CFLAGS} -c $^

self_net.o: self_net.c
	${CC} ${CFLAGS} -c $^


clean:
	rm -f *.o testMaster testSelfNet