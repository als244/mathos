CC = gcc
CFLAGS = -g -std=c99 -Wall -pedantic 

all: testMaster testWorker

## executables


## MASTER PROGRAM
testMaster: main_master.c table.o fifo.o deque.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o master.o utils.o cq_handler.o ctrl_handler.o work_pool.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread -libverbs

master.o: master.c
	${CC} ${CFLAGS} -c $^


## WORKER PROGRAM

testWorker: main_worker.c table.o fifo.o deque.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o join_net.o init_net.o utils.o cq_handler.o ctrl_handler.o fingerprint.o exchange.o exchange_worker.o work_pool.o init_sys.o
	${CC} ${CFLAGS} $^ -o $@ -lm -pthread -libverbs -lcrypto

join_net.o: join_net.c
	${CC} ${CFLAGS} -c $^

init_net.o: init_net.c
	${CC} ${CFLAGS} -c $^

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^

exchange.o: exchange.c
	${CC} ${CFLAGS} -c $^

exchange_worker.o: exchange_worker.c
	${CC} ${CFLAGS} -c $^

init_sys.o: init_sys.c
	${CC} ${CFLAGS} -c $^



## BOTH

table.o: table.c
	${CC} ${CFLAGS} -c $^

fifo.o: fifo.c
	${CC} ${CFLAGS} -c $^

deque.o: deque.c
	${CC} ${CFLAGS} -c $^

verbs_ops.o: verbs_ops.c
	${CC} ${CFLAGS} -c $^

ctrl_channel.o: ctrl_channel.c
	${CC} ${CFLAGS} -c $^

self_net.o: self_net.c
	${CC} ${CFLAGS} -c $^

net.o: net.c
	${CC} ${CFLAGS} -c $^

rdma_init_info.o: rdma_init_info.c
	${CC} ${CFLAGS} -c $^

tcp_connection.o: tcp_connection.c
	${CC} ${CFLAGS} -c $^

tcp_rdma_init.o: tcp_rdma_init.c
	${CC} ${CFLAGS} -c $^

utils.o: utils.c
	${CC} ${CFLAGS} -c $^

cq_handler.o: cq_handler.c
	${CC} ${CFLAGS} -c $^

ctrl_handler.o: ctrl_handler.c
	${CC} ${CFLAGS} -c $^



work_pool.o: work_pool.c
	${CC} ${CFLAGS} -c $^



## cleaning up


clean:
	rm -f *.o testMaster testWorker
