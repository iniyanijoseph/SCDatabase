all: dbserver.o dbclient.o
	gcc dbserver.o -o dbserver -Wall -Werror -std=gnu99 -pthread
	gcc dbclient.o -o dbclient -Wall -Werror -std=gnu99

dbserver.o: msg.h dbserver.c
	gcc -c dbserver.c

dbclient.o: msg.h dbclient.c
	gcc -c dbclient.c

clean:
	rm -f dbserver.o dbclient.o dbclient dbserver

