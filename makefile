CC = gcc
CFLAGS = -Wall -ansi -pedantic
TARGET = gbn
SRCS = emulator.c gbn.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
