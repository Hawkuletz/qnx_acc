# Build qdump using qnx_acc
CC = gcc
CCFLAGS = -Wall -O2
PROGRAM = qdump

all: qdump

qdump	: qdump.c qnx_acc.h qnx_acc.o
	$(CC) $(CCFLAGS) qdump.c qnx_acc.o -o qdump

qnx_acc.o	: qnx_acc.c qnx_acc.h
	$(CC) $(CCFLAGS) -c qnx_acc.c

clean:
	rm -f qnx_acc.o qdump
