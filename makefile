# makefile for this mini-project
#

UNAME := $(shell uname)
CC := gcc
ifeq ($(UNAME), Darwin)
	SED = gsed
	CC  = gcc-9
endif
CFLAGS = -std=c99 -O3 -lm
STREAMC = encoder.o galois.o decoder.o mt19937ar.o

libstreamc.a: $(STREAMC)
	ar rcs $@ $^

streamcTestIrreg: $(STREAMC) test_irregular.c
	$(CC) -o $@ $^ $(CFLAGS)

#streamcTestGilbert: $(STREAMC) test_gilbert.c
#	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)


.PHONY: clean

clean:
	rm -f *.o streamcTestIrreg libstreamc.a
