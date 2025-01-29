CC = gcc

DEV_CFLAGS = -g -std=c99 -Wall -pedantic
BUILD_CFLAGS = -O3 -std=c99 -Wall -pedantic
CFLAGS = ${DEV_CFLAGS}

HSA_CFLAGS = -D__HIP_PLATFORM_AMD__ 
HSA_INCLUDE = /opt/rocm/include
HSA_LIB = /opt/rocm/lib -lhsa-runtime64



CUDA_CFLAGS = -D__HIP_PLATFORM_NVIDIA__
CUDA_INCLUDE = /usr/local/cuda/include
CUDA_LIB = /usr/local/cuda/lib64

BACKEND_OBJS = cuda_memory.o backend_funcs.o backend_streams.o backend_profile.o cublas_helper.o


DEV_NVCC_FLAGS = -g -G
BUILD_NVCC_FLAGS = -O4 --use_fast_math
PROF_NVCC_FLAGS = -O4 --use_fast_math --generate-line-info -Xptxas=-v
NVCC_FLAGS = ${PROF_NVCC_FLAGS}

BACKEND_KERNELS = cuda_kernels_cubin cuda_kernels_ptx
BACKEND_INCLUDE = ${CUDA_INCLUDE}

BACKEND_CFLAGS = ${CUDA_CFLAGS}
BACKEND_LIB_PATH = ${CUDA_LIB}
BACKEND_LIB_LINKS = -lcuda -lcublasLt

BACKEND_MEMORY_OBJ = cuda_memory.o


EXECS = testMaster testWorker1 testBw

all: ${EXECS} ${BACKEND_KERNELS}

## executables


## MASTER PROGRAM
testMaster: main_master.c fast_table.o fast_tree.o fast_list.o table.o fifo.o deque.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o master.o utils.o cq_handler.o ctrl_handler.o work_pool.o master_worker.o ctrl_recv_dispatch.o
	${CC} ${CFLAGS} $^ -o $@ -pthread -libverbs -lcrypto -ldl

master.o: master.c
	${CC} ${CFLAGS} -c $^

master_worker.o: master_worker.c
	${CC} ${CFLAGS} -c $^


## WORKER PROGRAM
testWorker1: main_worker_1.c fast_table.o fast_tree.o fast_list.o table.o fifo.o deque.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o join_net.o init_net.o utils.o cq_handler.o ctrl_handler.o fingerprint.o exchange.o exchange_worker.o memory.o memory_server.o memory_client.o inventory.o inventory_worker.o work_pool.o sys.o ctrl_recv_dispatch.o exchange_client.o backend_funcs.o backend_streams.o backend_profile.o ${BACKEND_MEMORY_OBJ}
	${CC} ${CFLAGS} ${BACKEND_CFLAGS} $^ -o $@ -I $(BACKEND_INCLUDE) -pthread -libverbs -lcrypto -ldl -L $(BACKEND_LIB_PATH) ${BACKEND_LIB_LINKS}

## JUST FOR NOW INCLUDING BACKEND LINK WHILE INTERFACE IS UNDERWAY...
testWorker2: main_worker_2.c fast_table.o fast_tree.o fast_list.o table.o fifo.o deque.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o join_net.o init_net.o utils.o cq_handler.o ctrl_handler.o fingerprint.o exchange.o exchange_worker.o memory.o memory_server.o memory_client.o inventory.o inventory_worker.o work_pool.o sys.o ctrl_recv_dispatch.o exchange_client.o ${BACKEND_MEMORY_OBJ}
	${CC} ${CFLAGS} ${BACKEND_CFLAGS} $^ -o $@ -I $(BACKEND_INCLUDE) -pthread -libverbs -lcrypto -ldl -L $(BACKEND_LIB_PATH) ${BACKEND_LIB_LINKS}

testBw: main_test_bw.c fast_table.o fast_tree.o fast_list.o table.o fifo.o deque.o verbs_ops.o ctrl_channel.o self_net.o net.o rdma_init_info.o tcp_connection.o tcp_rdma_init.o join_net.o init_net.o utils.o cq_handler.o ctrl_handler.o fingerprint.o exchange.o exchange_worker.o memory.o memory_server.o memory_client.o inventory.o inventory_worker.o work_pool.o sys.o ctrl_recv_dispatch.o exchange_client.o backend_funcs.o backend_streams.o backend_profile.o ${BACKEND_MEMORY_OBJ}
	${CC} ${CFLAGS} ${BACKEND_CFLAGS} $^ -o $@ -I $(BACKEND_INCLUDE) -pthread -libverbs -lcrypto -ldl -L $(BACKEND_LIB_PATH) ${BACKEND_LIB_LINKS}













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

memory_server.o: memory_server.c
	${CC} ${CFLAGS} -c $^

memory_client.o: memory_client.c
	${CC} ${CFLAGS} -c $^

inventory.o: inventory.c
	${CC} ${CFLAGS} -c $^

inventory_worker.o: inventory_worker.c
	${CC} ${CFLAGS} -c $^

## JUST FOR NOW INCLUDING BACKEND LINK WHILE INTERFACE IS UNDERWAY...
sys.o: sys.c
	${CC} ${CFLAGS} -c $^ -I $(BACKEND_INCLUDE)

exchange_client.o: exchange_client.c
	${CC} ${CFLAGS} -c $^ -I $(BACKEND_INCLUDE)









## BACKENDS

backend_funcs.o: backend_funcs.c
	${CC} ${CFLAGS} -c $^ -I $(BACKEND_INCLUDE)

backend_streams.o: backend_streams.c
	${CC} ${CFLAGS} -c $^ -I $(BACKEND_INCLUDE)

backend_profile.o: backend_profile.c
	${CC} ${CFLAGS} -c $^ -I $(BACKEND_INCLUDE)
	

## CUDA SPECIFIC

cuda_memory.o: cuda_memory.c
	${CC} ${CFLAGS} -c $^ -I $(CUDA_INCLUDE)

cublas_helper.o: cublas_helper.c
	${CC} ${CFLAGS} -c $^ -I $(CUDA_INCLUDE)

cuda_kernels_ptx: cuda_kernels.cu
	nvcc ${NVCC_FLAGS} -ptx cuda_kernels.cu -arch=compute_86 -code=sm_86

cuda_kernels_cubin: cuda_kernels.cu
	nvcc ${NVCC_FLAGS} -cubin cuda_kernels.cu -arch=compute_86 -code=sm_86



# HSA SPECIFIC

hsa_memory.o: hsa_memory.c
	${CC} ${CFLAGS} -c $^ -I $(HSA_INCLUDE)

rocblas_funcs.o: rocblas_funcs.c
	${CC} ${CFLAGS} ${HSA_CFLAGS} -c $^ -I $(HSA_INCLUDE)

## BOTH

fast_table.o: fast_table.c
	${CC} ${CFLAGS} -c $^

fast_tree.o: fast_tree.c
	${CC} ${CFLAGS} -c $^

fast_list.o: fast_list.c
	${CC} ${CFLAGS} -c $^

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


ctrl_recv_dispatch.o: ctrl_recv_dispatch.c
	${CC} ${CFLAGS} -c $^





## cleaning up


clean:
	rm -f *.o ${EXECS} ${BACKEND_KERNELS}
