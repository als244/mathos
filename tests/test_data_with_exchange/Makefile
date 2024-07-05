CC = gcc
CFLAGS = -g -Wall -pedantic

all: testDataExchangeClient_0 testDataExchangeClient_1

testDataExchangeClient_0: main_0.c table.o deque.o fingerprint.o communicate.o exchange.o exchange_client.o channel.o data_channel.o data_controller.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm -libverbs -lrdmacm

testDataExchangeClient_1: main_1.c table.o deque.o fingerprint.o communicate.o exchange.o exchange_client.o channel.o data_channel.o data_controller.o
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

data_channel.o: data_channel.c
	${CC} ${CFLAGS} -c $^

data_controller.o: data_controller.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f testDataExchangeClient_0 testDataExchangeClient_1 *.o
