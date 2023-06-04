CFLAGS = -Wall -O -g

all: studyhelper
studyhelper: studyhelper.o
	gcc studyhelper.o -o studyhelper -lwiringPi -lpthread
clean:
	rm -f studyhelper *.o
