# Build qdump using qnx_acc
CC = gcc
CCFLAGS = -Wall -O2

all: qdump qobj

qdump	: qdump.c qnx_acc.h qnx_acc.o
	$(CC) $(CCFLAGS) qdump.c qnx_acc.o -o qdump

qnx_acc.o	: qnx_acc.c qnx_acc.h
	$(CC) $(CCFLAGS) -c qnx_acc.c

qobj	: qobj.c qnx_file.h
	$(CC) $(CCFLAGS) qobj.c -o qobj

clean:
	rm -f qnx_acc.o qdump qobj
