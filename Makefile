CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2
TARGET = avmsed

SRCS = common.c main.c parallel.c sequential.c
OBJS = $(SRCS:.c=.o)
HEADERS = common.h parallel.h sequential.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(TARGET)

.PHONY: all clean
