CC = gcc
CFLAGS = -g -Wall -pedantic

all: testExchange

testExchange: main.c table.o deque.o fingerprint.o exchange.o connection.o function.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm -libverbs -lrdmacm

table.o: table.c
	${CC} ${CFLAGS} -c $^

deque.o: deque.c
	${CC} ${CFLAGS} -c $^ 

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^

exchange.o: exchange.c
	${CC} ${CFLAGS} -c $^

connection.o: connection.c
	${CC} ${CFLAGS} -c $^

function.o: function.c
	${CC} ${CFLAGS} -c $^ 

clean:
	rm -f testExchange *.o
