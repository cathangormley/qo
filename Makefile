CC := gcc
CFLAGS := -std=c17 -D_POSIX_C_SOURCE=200809L -O3 -march=native -funroll-loops -flto -fno-math-errno -Wall -Wextra
LDFLAGS := -lm

SRC_DIR := src
BUILD_DIR := build
BIN := $(BUILD_DIR)/qo
SRCS := $(wildcard $(SRC_DIR)/*.c)

.PHONY: all build run test clean

all: build

build: $(BIN)

$(BIN): $(SRCS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: build
	rlwrap $(BIN) $(ARGS)

test: build
	QO_BIN=./$(BIN) bash test/run_tests.sh

clean:
	rm -rf $(BUILD_DIR)
