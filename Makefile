CFLAGS = -Wall -O -g

all: smartlab
smartlab: smartlab.o
	gcc smartlab.o -o smartlab -lwiringPi -lpthread
clean:
	rm -f smartlab *.o
