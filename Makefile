CFLAGS = -Wall -O -g

all: studyhelper
smartlab: smartlab.o
	gcc smartlab.o -o smartlab -lwiringPi -lpthread
clean:
	rm -f smartlab *.o
