CC = gcc
CFLAGS = -g -Wall -pedantic

all: testCQ_0 testCQ_1

testCQ_0: main_0.c communicate.o
	${CC} ${CFLAGS} $^ -o $@ -libverbs -lrdmacm

testCQ_1: main_1.c communicate.o
	${CC} ${CFLAGS} $^ -o $@ -libverbs -lrdmacm

communicate.o: communicate.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f testCQ_0 testCQ_1 *.o
