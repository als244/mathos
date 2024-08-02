CC = gcc
CFLAGS = -g -Wall -pedantic

all: testExchangeClient_0 testExchangeClient_1

testExchangeClient_0: main_0.c table.o deque.o fingerprint.o communicate.o exchange.o exchange_client.o channel.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm -libverbs -lrdmacm

testExchangeClient_1: main_1.c table.o deque.o fingerprint.o communicate.o exchange.o exchange_client.o channel.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm -libverbs -lrdmacm

table.o: table.c
	${CC} ${CFLAGS} -c $^

deque.o: deque.c
	${CC} ${CFLAGS} -c $^

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^

communicate.o: communicate.c
	${CC} ${CFLAGS} -c $^

exchange.o: exchange.c
	${CC} ${CFLAGS} -c $^

exchange_client.o: exchange_client.c
	${CC} ${CFLAGS} -c $^

channel.o: channel.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f testExchangeClient_0 testExchangeClient_1 *.o
