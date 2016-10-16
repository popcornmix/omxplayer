CC ?= gcc
CFLAGS ?= -Wall -std=gnu99

ifeq ($(strip $(DEBUG)),1)
CFLAGS += -DDEBUG -g
endif

OBJS = arg_parser.o omx_cmd.o
EXEC = omx_api

all: $(OBJS) $(EXEC)

SRCS = main.c arg_parser.c omx_cmd.c

%.o: %.c
	$(CC) $(CFLAGS) -c $^

omx_api: main.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	$(RM) $(EXEC) $(OBJS)
