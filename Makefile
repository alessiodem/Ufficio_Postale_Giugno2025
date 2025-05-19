CC := gcc
CFLAGS := -O0 -g -Wall
LDFLAGS := -lm # target link libraries
BUILD_DIR := ./build

all: manager ticket_dispenser user worker

sem_handling.o: lib/sem_handling.c
	$(CC) -c lib/sem_handling.c -o $(BUILD_DIR)/$@

utils.o: lib/utils.c
	$(CC) -c lib/utils.c -o $(BUILD_DIR)/$@

manager: src/manager.c sem_handling.o utils.o
	$(CC) src/manager.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o $(LDFLAGS) $(CFLAGS) -o $(BUILD_DIR)/$@

ticket_dispenser: src/ticket_dispenser.c sem_handling.o utils.o
	$(CC) src/ticket_dispenser.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o $(LDFLAGS) $(CFLAGS) -o $(BUILD_DIR)/$@

user: src/user.c sem_handling.o utils.o
	$(CC) src/user.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o $(LDFLAGS) $(CFLAGS) -o $(BUILD_DIR)/$@

worker: src/worker.c sem_handling.o utils.o
	$(CC) src/worker.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o $(LDFLAGS) $(CFLAGS) -o $(BUILD_DIR)/$@