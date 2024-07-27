CC = gcc
CFLAGS = -g -std=c99 -Wall -pedantic 

all: testMaster testWorker

## executables


## MASTER PROGRAM
testMaster: main_master.c table.o self_net.o net.o rdma_init_info.o tcp_rdma_init.o master.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread -libverbs

master.o: master.c
	${CC} ${CFLAGS} -c $^


## WORKER PROGRAM

testWorker: main_worker.c table.o self_net.o net.o rdma_init_info.o tcp_rdma_init.o tcp_connection.o join_net.o init_net.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread -libverbs 

tcp_connection.o: tcp_connection.c
	${CC} ${CFLAGS} -c $^

join_net.o: join_net.c
	${CC} ${CFLAGS} -c $^

init_net.o: init_net.c
	${CC} ${CFLAGS} -c $^


## BOTH

table.o: table.c
	${CC} ${CFLAGS} -c $^

self_net.o: self_net.c
	${CC} ${CFLAGS} -c $^

net.o: net.c
	${CC} ${CFLAGS} -c $^

rdma_init_info.o: rdma_init_info.c
	${CC} ${CFLAGS} -c $^

tcp_rdma_init.o: tcp_rdma_init.c
	${CC} ${CFLAGS} -c $^


clean:
	rm -f *.o testMaster testWorker
