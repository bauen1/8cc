CFLAGS=-Wall -Wno-strict-aliasing -std=gnu11 -g -I. -O0 -fsanitize=undefined -fno-omit-frame-pointer
OBJS=cpp.o debug.o dict.o gen.o lex.o vector.o parse.o buffer.o map.o \
     error.o path.o file.o set.o encoding.o
TESTS := $(patsubst %.c,%.bin,$(filter-out test/testmain.c,$(wildcard test/*.c)))
ECC=./8cc
override CFLAGS += -DBUILD_DIR='"$(shell pwd)"' -DSYSROOT_DIR='"$(shell pwd)/libruntime"'

LDFLAGS += -lubsan

8cc: 8cc.h main.o $(OBJS)
	cc -o $@ main.o $(OBJS) $(LDFLAGS)

$(OBJS) utiltest.o main.o: 8cc.h keyword.inc

utiltest: 8cc.h utiltest.o $(OBJS)
	cc -o $@ utiltest.o $(OBJS) $(LDFLAGS)

test/%.o: test/%.c $(ECC)
	$(ECC) -w -o $@ -c $<

test/%.bin: test/%.o test/testmain.o
	cc -o $@ $< test/testmain.o $(LDFLAGS)

self: 8cc cleanobj
	$(MAKE) CC=$(ECC) CFLAGS= 8cc

test-65816/%.s: test-65816/%.c 8cc
	./8cc -S -o $@ $<

test-65816/%.test: test-65816/%.s test-65816/%.output
	cmp -s test-65816/$*.s test-65816/$*.output && touch $@ || exit 1

test: 8cc $(TESTS)

#test: 8cc $(TESTS)
#	$(MAKE) CC=$(ECC) CFLAGS= utiltest
#	./utiltest
#	./test/ast.sh
#	./test/negative.py
#	$(MAKE) runtests

runtests:
	@for test in $(TESTS); do  \
	    ./$$test || exit;      \
	done

stage1:
	$(MAKE) cleanobj
	[ -f 8cc ] || $(MAKE) 8cc
	mv 8cc stage1

stage2: stage1
	$(MAKE) cleanobj
	$(MAKE) CC=./stage1 ECC=./stage1 CFLAGS= 8cc
	mv 8cc stage2

stage3: stage2
	$(MAKE) cleanobj
	$(MAKE) CC=./stage2 ECC=./stage2 CFLAGS= 8cc
	mv 8cc stage3

# Compile and run the tests with the default compiler.
testtest:
	$(MAKE) clean
	$(MAKE) $(TESTS)
	$(MAKE) runtests

fulltest: testtest
	$(MAKE) stage1
	$(MAKE) CC=./stage1 ECC=./stage1 CFLAGS= test
	$(MAKE) stage2
	$(MAKE) CC=./stage2 ECC=./stage2 CFLAGS= test
	$(MAKE) stage3
	cmp stage2 stage3

clean: cleanobj
	rm -f 8cc stage?

cleanobj:
	rm -f *.o *.s test/*.o test/*.bin utiltest

LIBRUNTIME_OBJS := libruntime/strlen.o

CA65 ?= ca65
AR65 ?= ar65

CA65_FLAGS := --cpu 65816 --memory-model huge

libruntime/%.o libruntime/%.list: libruntime/%.s
	$(CA65) $(CA65_FLAGS) --listing libruntime/$*.list -o libruntime/$*.o $<

libruntime/libruntime.lib: $(LIBRUNTIME_OBJS)
	$(AR65) a $@ $?

all: 8cc

.PHONY: clean cleanobj test runtests fulltest self all
