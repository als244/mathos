CC = gcc
CFLAGS = -g -std=c99 -Wall -pedantic 

all: testEstablishNet

testEstablishNet: main.c establish_net.o net.o node_ip_config.o self_net.o table.o
	${CC} ${CFLAGS} $^ -o $@ -lm -libverbs -pthread

establish_net.o: establish_net.c
	${CC} ${CFLAGS} -c $^

net.o: net.c
	${CC} ${CFLAGS} -c $^

node_ip_config.o: node_ip_config.c
	${CC} ${CFLAGS} -c $^

self_net.o: self_net.c
	${CC} ${CFLAGS} -c $^

table.o: table.c
	${CC} ${CFLAGS} -c $^

clean:
	rm -f *.o testEstablishNet
