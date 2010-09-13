CC = g++
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)
OBJS = m4mudex.o
DIST = test.m4a Makefile m4mudex.cc README

m4mudex: m4mudex.o
	$(CC) $(FLAGS) $(OBJS) -o m4mudex

m4mudex.o: m4mudex.cc
	$(CC) $(CFLAGS) m4mudex.cc

test: m4mudex
	./m4mudex test.m4a test-metaless.m4a
	open test-metaless.m4a

clean: 
	$(RM) m4mudex m4mudex.o test-metaless.m4a m4mudex.tar.gz


pkg: $(DIST) 
	tar -cvf m4mudex.tar.gz $(DIST) 
