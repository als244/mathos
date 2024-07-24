CC = gcc
CFLAGS = -g -std=c99 -Wall -pedantic 

all: testMaster testJoinNet

## executables


## MASTER PROGRAM
testMaster: main_master.c master.o table.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread

master.o: master.c
	${CC} ${CFLAGS} -c $^


## WORKER PROGRAM

testJoinNet: main_worker.c join_net.o
	${CC} ${CFLAGS} $^ -o $@ -pthread


join_net.o: join_net.c
	${CC} ${CFLAGS} -c $^


## BOTH

table.o: table.c
	${CC} ${CFLAGS} -c $^


clean:
	rm -f *.o testMaster testJoinNet
