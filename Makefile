
PROGRAM=client3
PROGRAM2=server3
TESTER=as2_testbench
CFLAGS=-O2 -g -Wall -pedantic -pthread

all: ${TESTER} ${PROGRAM} ${PROGRAM2}

${TESTER}: ${TESTER}.c linebuffer.c

.PHONY: launch
launch:
	./${PROGRAM2}

.PHONY: test
test: all
	./${TESTER} ${PROGRAM}

.PHONY: clean
clean:
	rm -rf *.o *~ ${TESTER} ${PROGRAM} ${PROGRAM2}
