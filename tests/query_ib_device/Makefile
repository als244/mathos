CC = gcc
CFLAGS = -g -Wall -pedantic

all: queryIBDev

queryIBDev: query_ib_device.c
	${CC} ${CFLAGS} $^ -o $@ -libverbs

clean:
	rm -f queryIBDev *.o