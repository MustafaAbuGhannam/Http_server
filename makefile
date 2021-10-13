all: server.c threadpool.c
	 	gcc threadpool.c server.c -o server -Wall -g -lpthread 
all-GDB: threadpool.c server.c
	 	gcc -g threadpool.c server.c -o server -Wall -g -lpthread