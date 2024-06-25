CC = gcc
CFLAGS = -O3 -Wall -pedantic

all: testTable

testTable: main.c table.o fingerprint.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm

table.o: table.c
	${CC} ${CFLAGS} -c $^ 

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^ 

clean:
	rm -f testTable *.o
