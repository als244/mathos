CC = gcc
CFLAGS = -g -Wall -pedantic

all: testExchangeClient

testExchangeClient: main.c table.o deque.o fingerprint.o connection.o exchange.o exchange_client.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm -libverbs -lrdmacm

table.o: table.c
	${CC} ${CFLAGS} -c $^

deque.o: deque.c
	${CC} ${CFLAGS} -c $^

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^

connection.o: connection.c
	${CC} ${CFLAGS} -c $^

exchange.o: exchange.c
	${CC} ${CFLAGS} -c $^

exchange_client.o: exchange_client.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f testExchangeClient *.o
