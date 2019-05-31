# makefile for this mini-project
#

UNAME := $(shell uname)
CC := gcc
ifeq ($(UNAME), Darwin)
	SED = gsed
	CC  = gcc-8
endif
CFLAGS = -std=c99 -g -lm
STREAMC = encoder.o galois.o decoder.o

streamcTest: $(STREAMC) test.c
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)


.PHONY: clean

clean:
	rm -f *.o streamcTest