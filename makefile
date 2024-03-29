# makefile for this mini-project
#

UNAME := $(shell uname)
CC := gcc
ifeq ($(UNAME), Darwin)
	SED = gsed
	CC  = clang
	HAS_SSSE3 := $(shell sysctl -a | grep supplementalsse3)
	HAS_AVX2  := $(shell sysctl -a | grep avx2)
endif
ifeq ($(UNAME), Linux)
	SED = sed
	CC  = gcc
	HAS_NEON32  := $(shell grep -i neon /proc/cpuinfo)
	HAS_NEON64  := $(shell uname -a | grep -i aarch64)
	HAS_SSSE3 := $(shell grep -i ssse3 /proc/cpuinfo)
	HAS_AVX2  := $(shell grep -i avx2 /proc/cpuinfo)
endif
CFLAGS0 = -std=c99 -O3 -lm -DDEBUG
#CFLAGS0 = -std=c99 -O3 -lm
# Add SIMD compiler options
ifneq ($(HAS_SSSE3),)
	CFLAGS1 = -mssse3 -DINTEL_SSSE3
endif
ifneq ($(HAS_AVX2),)
	CFLAGS1 += -mavx2 -DINTEL_AVX2
endif

STREAMC = encoder.o galois.o decoder.o mt19937ar.o

libstreamc.a: $(STREAMC)
	ar rcs $@ $^

libstreamc.so: $(STREAMC)
	$(CC) -shared -o libstreamc.so $^

streamcTestBernouFull: $(STREAMC) examples/test.bernoulli.full.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestGEloss: $(STREAMC) examples/test.gilbert.full.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestBlossFull: $(STREAMC) examples/test.bloss.full.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestBernouShort: $(STREAMC) examples/test.bernoulli.short.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestBlossShort: $(STREAMC) examples/test.bloss.short.c
	$(CC) -o $@ $^ $(CFLAGS0)


%.o: %.c $(DEPS)
	$(CC) -c -fPIC -o $@ $< $(CFLAGS0) $(CFLAGS1)


.PHONY: clean

clean:
	rm -f *.o streamcTest* libstreamc.a libstreamc.so
