CC = gcc

CFLAGS = -g -std=c99 -Wall -pedantic -fno-omit-frame-pointer

HSA_CFLAGS = -D__HIP_PLATFORM_AMD__ 

HSA_INCLUDE = /opt/rocm/include
HSA_LIB = /opt/rocm/lib -lhsa-runtime64

all: testMaster testWorker1 testWorker2

## executables


## MASTER PROGRAM
testMaster: main_master.c table.o fifo.o deque.o skiplist.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o master.o utils.o cq_handler.o ctrl_handler.o work_pool.o master_worker.o ctrl_recv_dispatch.o
	${CC} ${CFLAGS} $^ -o $@ -pthread -libverbs

master.o: master.c
	${CC} ${CFLAGS} -c $^

master_worker.o: master_worker.c
	${CC} ${CFLAGS} -c $^


## WORKER PROGRAM

testWorker1: main_worker_1.c table.o fifo.o deque.o skiplist.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o join_net.o init_net.o utils.o cq_handler.o ctrl_handler.o fingerprint.o exchange.o exchange_worker.o memory.o inventory.o inventory_worker.o work_pool.o sys.o ctrl_recv_dispatch.o exchange_client.o hsa_memory.o rocblas_funcs.o
	${CC} ${CFLAGS} ${HSA_CFLAGS} $^ -o $@ -I $(HSA_INCLUDE) -pthread -libverbs -lcrypto -L $(HSA_LIB) -lamdhip64 -lrocblas

testWorker2: main_worker_2.c table.o fifo.o deque.o skiplist.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o join_net.o init_net.o utils.o cq_handler.o ctrl_handler.o fingerprint.o exchange.o exchange_worker.o memory.o inventory.o inventory_worker.o work_pool.o sys.o ctrl_recv_dispatch.o exchange_client.o
	${CC} ${CFLAGS} $^ -o $@ -pthread -libverbs -lcrypto

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

memory.o: memory.c
	${CC} ${CFLAGS} -c $^

inventory.o: inventory.c
	${CC} ${CFLAGS} -c $^

inventory_worker.o: inventory_worker.c
	${CC} ${CFLAGS} -c $^

sys.o: sys.c
	${CC} ${CFLAGS} -c $^

exchange_client.o: exchange_client.c
	${CC} ${CFLAGS} -c $^

rocblas_funcs.o: rocblas_funcs.c
	${CC} ${CFLAGS} ${HSA_CFLAGS} -c $^ -I $(HSA_INCLUDE)



## BACKENDS

hsa_memory.o: hsa_memory.c
	${CC} ${CFLAGS} -c $^ -I $(HSA_INCLUDE)



## BOTH

table.o: table.c
	${CC} ${CFLAGS} -c $^

fifo.o: fifo.c
	${CC} ${CFLAGS} -c $^

deque.o: deque.c
	${CC} ${CFLAGS} -c $^

skiplist.o: skiplist.c
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


ctrl_recv_dispatch.o: ctrl_recv_dispatch.c
	${CC} ${CFLAGS} -c $^





## cleaning up


clean:
	rm -f *.o testMaster testWorker1 testWorker2 
