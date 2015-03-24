all: myhttpd

FLAGS=-g
INC= ./ 
LIBS= 

myhttpd: fdwatch.o timer.o libhttpd.o mmc.o myhttpd.o
	gcc $(FLAGS) -o $@ fdwatch.o timer.o libhttpd.o mmc.o myhttpd.o

fdwatch.o: fdwatch.c
	gcc $(FLAGS) -c fdwatch.c

timer.o: timer.c
	gcc $(FLAGS) -c timer.c

libhttpd.o: libhttpd.c
	gcc $(FLAGS) -c libhttpd.c

mmc.o: mmc.c
	gcc $(FLAGS) -c mmc.c

myhttpd.o: myhttpd.c
	gcc $(FLAGS) -c myhttpd.c

clean:
	rm -rf *.o myhttpd
