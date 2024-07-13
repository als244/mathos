CC = gcc
CFLAGS = -g -Wall -pedantic

all: testQP_0 testQP_1

testQP_0: main_0.c setup_net.o discover_remote_qps.o verbs_ops.o
	${CC} ${CFLAGS} $^ -o $@ -libverbs

testQP_1: main_1.c setup_net.o discover_remote_qps.o verbs_ops.o
	${CC} ${CFLAGS} $^ -o $@ -libverbs

setup_net.o: setup_net.c
	${CC} ${CFLAGS} -c $^

discover_remote_qps.o: discover_remote_qps.c
	${CC} ${CFLAGS} -c $^

verbs_ops.o: verbs_ops.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f testQP_0 testQP_1 *.o
