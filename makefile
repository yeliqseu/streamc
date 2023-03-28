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

streamcTest: $(STREAMC) test.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestGEloss: $(STREAMC) test.gilbert.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestBloss: $(STREAMC) test.bloss.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestShort: $(STREAMC) test.short.c
	$(CC) -o $@ $^ $(CFLAGS0)

streamcTestBlossShort: $(STREAMC) test.bloss.short.c
	$(CC) -o $@ $^ $(CFLAGS0)

#streamcTestGilbert: $(STREAMC) test_gilbert.c
#	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c $(DEPS)
	$(CC) -c -fPIC -o $@ $< $(CFLAGS0) $(CFLAGS1)


.PHONY: clean

clean:
	rm -f *.o streamcTest streamcTestGEloss streamcTestBloss streamcTestBlossShort streamcTestShort libstreamc.a libstreamc.so
