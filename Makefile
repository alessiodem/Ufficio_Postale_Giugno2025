CC := gcc
CFLAGS := -O0 -g -Wall
LDFLAGS := -lm
BUILD_DIR := ./build

all: $(BUILD_DIR)/manager $(BUILD_DIR)/ticket_dispenser $(BUILD_DIR)/user $(BUILD_DIR)/worker  $(BUILD_DIR)/utils.o $(BUILD_DIR)/sem_handling.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/sem_handling.o: lib/sem_handling.c | $(BUILD_DIR)
	$(CC) -c $< -o $@

$(BUILD_DIR)/utils.o: lib/utils.c | $(BUILD_DIR)
	$(CC) -c $< -o $@

$(BUILD_DIR)/manager: src/manager.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o  | $(BUILD_DIR)
	$(CC) $^ $(LDFLAGS) $(CFLAGS) -o $@

$(BUILD_DIR)/ticket_dispenser: src/ticket_dispenser.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o | $(BUILD_DIR)
	$(CC) $^ $(LDFLAGS) $(CFLAGS) -o $@

$(BUILD_DIR)/user: src/user.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o | $(BUILD_DIR)
	$(CC) $^ $(LDFLAGS) $(CFLAGS) -o $@

$(BUILD_DIR)/worker: src/worker.c $(BUILD_DIR)/sem_handling.o $(BUILD_DIR)/utils.o  | $(BUILD_DIR)
	$(CC) $^ $(LDFLAGS) $(CFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/manager $(BUILD_DIR)/ticket_dispenser $(BUILD_DIR)/user $(BUILD_DIR)/worker