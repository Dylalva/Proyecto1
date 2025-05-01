CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lpthread

EXECUTABLES = broker producer consumer

all: $(EXECUTABLES)

broker: src/broker.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

producer: src/producer.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

consumer: src/consumer.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(EXECUTABLES)
