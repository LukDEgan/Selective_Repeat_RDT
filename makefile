CC = gcc
CFLAGS = -Wall -ansi -pedantic
GBN_TARGET = gbn
SR_TARGET = sr

GBN_SRCS = emulator.c gbn.c
SR_SRCS = emulator.c sr.c

all: $(GBN_TARGET) $(SR_TARGET)

$(GBN_TARGET): $(GBN_SRCS)
	$(CC) $(CFLAGS) -o $(GBN_TARGET) $(GBN_SRCS)

$(SR_TARGET): $(SR_SRCS)
	$(CC) $(CFLAGS) -o $(SR_TARGET) $(SR_SRCS)

clean:
	rm -f $(GBN_TARGET) $(SR_TARGET)
