CC = gcc
CFLAGS = -O3 -Wall -pedantic

all: perfTable

perfTable: main.c table.o fingerprint.o database.o
	${CC} ${CFLAGS} $^ -o $@ -lcrypto -lm -lgdbm_compat -lgdbm -pthread

table.o: table.c
	${CC} ${CFLAGS} -c $^ 

fingerprint.o: fingerprint.c
	${CC} ${CFLAGS} -c $^ 

database.o: database.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f perfTable *.o
