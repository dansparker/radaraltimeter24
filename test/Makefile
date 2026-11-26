# Host tests (gcc or clang):  make            build and run
#                             make SAN=1      with address/undefined sanitizers
#                             make range      altitude range against the extended ground model
CC      ?= gcc
BUILD   ?= build
RDSP    := ../lib/rdsp
APP     := ../firmware/app
SRCS    := $(wildcard $(RDSP)/src/*.c) $(APP)/altimeter.c $(APP)/config.c $(APP)/ramp.c $(APP)/proto.c \
           sim.c test_dsp.c test_alt.c test_main.c
CFLAGS  := -std=c99 -O2 -g -Wall -Wextra -Wshadow -Wno-unused-parameter -Werror \
           -I$(RDSP)/include -I$(APP) -I. -D_DEFAULT_SOURCE
ifeq ($(SAN),1)
CFLAGS  += -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

all: run

$(BUILD)/run_tests: $(SRCS) $(wildcard *.h) $(wildcard $(APP)/*.h) $(wildcard $(RDSP)/include/rdsp/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -lm -o $@

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests

RSRCS   := $(wildcard $(RDSP)/src/*.c) $(APP)/altimeter.c $(APP)/config.c $(APP)/ramp.c sim.c range_sim.c

$(BUILD)/range_sim: $(RSRCS) $(wildcard *.h) $(wildcard $(APP)/*.h) $(wildcard $(RDSP)/include/rdsp/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(RSRCS) -lm -o $@

range: $(BUILD)/range_sim
	./$(BUILD)/range_sim
	./$(BUILD)/range_sim short

clean:
	rm -rf $(BUILD)

.PHONY: all run range clean
