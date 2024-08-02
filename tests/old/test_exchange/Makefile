CC = gcc
CFLAGS = -g -Wall -pedantic

all: testExchange

testExchange: main.c table.o deque.o fingerprint.o exchange.o function.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm

table.o: table.c
	${CC} ${CFLAGS} -c $^

deque.o: deque.c
	${CC} ${CFLAGS} -c $^ 

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^

exchange.o: exchange.c
	${CC} ${CFLAGS} -c $^

function.o: function.c
	${CC} ${CFLAGS} -c $^ 

clean:
	rm -f testExchange *.o
