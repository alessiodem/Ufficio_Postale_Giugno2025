CC := gcc
CFLAGS := -O0 -g #aggiungere quelle mie


all: manager user worker ticket_dispenser



sem_handling.o: sem_handling.c
	$(CC) -c sem_handling.c -o $@

utils.o: utils.c
	$(CC) -c utils.c -o $@

manager.o: manager.c common.h
	$(CC) -c manager.c $(CFLAGS) -o $@

manager: manager.o sem_handling.o utils.o
	$(CC) manager.o sem_handling.o utils.o $(CFLAGS) -o $@

user: user.c sem_handling.o utils.o
	$(CC) user.c sem_handling.o utils.o $(CFLAGS) -o $@

worker: worker.c sem_handling.o utils.o
	$(CC) worker.c sem_handling.o utils.o $(CFLAGS) -o $@

ticket_dispenser: ticket_dispenser.c sem_handling.o utils.o
	$(CC) ticket_dispenser.c sem_handling.o utils.o $(CFLAGS) -o $@

clean:
	rm -f *.o manager user worker ticket_dispenser