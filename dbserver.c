#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "msg.h"

#define RECORD_SIZE 256

typedef struct msg msg;
typedef struct record record;

void Usage(char *progname);
void PrintOut(int fd, struct sockaddr *addr, size_t addrlen);
void PrintReverseDNS(struct sockaddr *addr, size_t addrlen);
void PrintServerSide(int client_fd, int sock_family);

int  Listen(char *portnum, int *sock_family);
// void HandleClient(int c_fd, struct sockaddr *addr, size_t addrlen,
//                   int sock_family);
struct HandleClientArgs {
    int c_fd;
    struct sockaddr *addr;
    size_t addrnlen;
    int sockfamily;
};
typedef struct HandleClientArgs HandleClientArgs;
void *HandleClient(void *arg);

int DBGet(int32_t fd, record* message);
int DBPut(int32_t fd, record* message);
void PrintRecord(record *rd);
void PrintMessage(msg* message);

int dbFD;

int main(int argc, char **argv) {

  dbFD = open("db.txt", O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
  if (dbFD == -1) {
    perror("open failed");
    exit(EXIT_FAILURE);
  }

  // Expect the port number as a command line argument.
  if (argc != 2) {
    Usage(argv[0]);
  }

  int sock_family;
  int listen_fd = Listen(argv[1], &sock_family);
  if (listen_fd <= 0) {
    // We failed to bind/listen to a socket.  Quit with failure.
    printf("Couldn't bind to any addresses.\n");
    return EXIT_FAILURE;
  }

  // Loop forever, accepting a connection from a client and doing
  // an echo trick to it.
  while (1) {
    struct sockaddr_storage caddr;
    socklen_t caddr_len = sizeof(caddr);
    int client_fd = accept(listen_fd,
                           (struct sockaddr *)(&caddr),
                           &caddr_len);
    if (client_fd < 0) {
      if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK))
        continue;
      printf("Failure on accept:%s \n ", strerror(errno));
      break;
    }

    pthread_t clientThread;
    HandleClientArgs args; // Build argument list
    args.c_fd = client_fd;
    args.addr = (struct sockaddr *)(&caddr);
    args.addrnlen = caddr_len;
    args.sockfamily = sock_family;
    pthread_create(&clientThread, NULL, HandleClient, (void*)&args); // Create thread for handling a client
  }

  // Close socket
  close(listen_fd);
  close(dbFD);
  return EXIT_SUCCESS;
}

void Usage(char *progname) {
  printf("usage: %s port \n", progname);
  exit(EXIT_FAILURE);
}

void
PrintOut(int fd, struct sockaddr *addr, size_t addrlen) {
    printf("Socket [%d] is bound to: \n", fd);
    if (addr->sa_family == AF_INET) {
        // Print out the IPV4 address and port
        char astring[INET_ADDRSTRLEN];
        struct sockaddr_in *in4 = (struct sockaddr_in *)(addr);
        inet_ntop(AF_INET, &(in4->sin_addr), astring, INET_ADDRSTRLEN);
        printf(" IPv4 address %s", astring);
        printf(" and port %d\n", ntohs(in4->sin_port));
    } else if (addr->sa_family == AF_INET6) {
        // Print out the IPV6 address and port
        char astring[INET6_ADDRSTRLEN];
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)(addr);
        inet_ntop(AF_INET6, &(in6->sin6_addr), astring, INET6_ADDRSTRLEN);
        printf("IPv6 address %s", astring);
        printf(" and port %d\n", ntohs(in6->sin6_port));
    } else {
        printf(" ???? address and port ???? \n");
    }
}

void
PrintReverseDNS(struct sockaddr *addr, size_t addrlen) {
  char hostname[1024];  // ought to be big enough.
  if (getnameinfo(addr, addrlen, hostname, 1024, NULL, 0, 0) != 0) {
    sprintf(hostname, "[reverse DNS failed]");
  }
  printf("DNS name: %s \n", hostname);
}

void
PrintServerSide(int client_fd, int sock_family) {
  char hname[1024];
  hname[0] = '\0';

  printf("Server side interface is ");
  if (sock_family == AF_INET) {
    // The server is using an IPv4 address.
    struct sockaddr_in srvr;
    socklen_t srvrlen = sizeof(srvr);
    char addrbuf[INET_ADDRSTRLEN];
    getsockname(client_fd, (struct sockaddr *) &srvr, &srvrlen);
    inet_ntop(AF_INET, &srvr.sin_addr, addrbuf, INET_ADDRSTRLEN);
    printf("%s", addrbuf);
    // Get the server's dns name, or return it's IP address as
    // a substitute if the dns lookup fails.
    getnameinfo((const struct sockaddr *) &srvr,
                srvrlen, hname, 1024, NULL, 0, 0);
    printf(" [%s]\n", hname);
  } else {
    // The server is using an IPv6 address.
    struct sockaddr_in6 srvr;
    socklen_t srvrlen = sizeof(srvr);
    char addrbuf[INET6_ADDRSTRLEN];
    getsockname(client_fd, (struct sockaddr *) &srvr, &srvrlen);
    inet_ntop(AF_INET6, &srvr.sin6_addr, addrbuf, INET6_ADDRSTRLEN);
    printf("%s", addrbuf);
    // Get the server's dns name, or return it's IP address as
    // a substitute if the dns lookup fails.
    getnameinfo((const struct sockaddr *) &srvr,
                srvrlen, hname, 1024, NULL, 0, 0);
    printf(" [%s]\n", hname);
  }
}

int Listen(char *portnum, int *sock_family) {

  // Populate the "hints" addrinfo structure for getaddrinfo().
  // ("man addrinfo")
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;       // IPv6 (also handles IPv4 clients)
  hints.ai_socktype = SOCK_STREAM;  // stream
  hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
  hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
  hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  // Use argv[1] as the string representation of our portnumber to
  // pass in to getaddrinfo().  getaddrinfo() returns a list of
  // address structures via the output parameter "result".
  struct addrinfo *result;
  int res = getaddrinfo(NULL, portnum, &hints, &result);

  // Did addrinfo() fail?
  if (res != 0) {
	printf( "getaddrinfo failed: %s", gai_strerror(res));
    return -1;
  }

  // Loop through the returned address structures until we are able
  // to create a socket and bind to one.  The address structures are
  // linked in a list through the "ai_next" field of result.
  int listen_fd = -1;
  struct addrinfo *rp;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    listen_fd = socket(rp->ai_family,
                       rp->ai_socktype,
                       rp->ai_protocol);
    if (listen_fd == -1) {
      // Creating this socket failed.  So, loop to the next returned
      // result and try again.
      printf("socket() failed:%s \n ", strerror(errno));
      listen_fd = -1;
      continue;
    }

    // Configure the socket; we're setting a socket "option."  In
    // particular, we set "SO_REUSEADDR", which tells the TCP stack
    // so make the port we bind to available again as soon as we
    // exit, rather than waiting for a few tens of seconds to recycle it.
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    // Try binding the socket to the address and port number returned
    // by getaddrinfo().
    if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      // Bind worked!  Print out the information about what
      // we bound to.
      PrintOut(listen_fd, rp->ai_addr, rp->ai_addrlen);

      // Return to the caller the address family.
      *sock_family = rp->ai_family;
      break;
    }
    printf("Couldn't bind to port number %s\n", portnum);
    hints.ai_family = AF_INET;       // IPv6 (also handles IPv4 clients)
    hints.ai_socktype = SOCK_STREAM;  // stream
    hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
    hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
    hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int random_num = (rand() % (10000 - 2000 + 1)) + 2000;
    char random_num_str[11];
    sprintf(random_num_str, "%d", random_num);

    struct addrinfo *result;
    int res = getaddrinfo(NULL, random_num_str, &hints, &result);

    // Did addrinfo() fail?
    if (res != 0) {
    printf( "getaddrinfo failed: %s", gai_strerror(res));
      return -1;
    }

    rp->ai_next = result;

    // The bind failed.  Close the socket, then loop back around and
    // try the next address/port returned by getaddrinfo().
    close(listen_fd);
    listen_fd = -1;
  }

  // Free the structure returned by getaddrinfo().
  freeaddrinfo(result);

  // If we failed to bind, return failure.
  if (listen_fd == -1)
    return listen_fd;

  // Success. Tell the OS that we want this to be a listening socket.
  if (listen(listen_fd, SOMAXCONN) != 0) {
    printf("Failed to mark socket as listening:%s \n ", strerror(errno));
    close(listen_fd);
    return -1;
  }

  // Return to the client the listening file descriptor.
  return listen_fd;
}

void* HandleClient(void *arg) {
  // Print out information about the client.
    HandleClientArgs* args = (HandleClientArgs*)arg;
    int c_fd = args->c_fd;
    struct sockaddr *addr = args->addr;
    size_t addrlen = args->addrnlen;
    int sock_family = args->sockfamily;

    printf("\nNew client connection \n" );
    PrintOut(c_fd, addr, addrlen);
    PrintReverseDNS(addr, addrlen);
    PrintServerSide(c_fd, sock_family);

    // Loop, reading data and echo'ing it back, until the client
    // closes the connection.
    while (1) {
        msg message;
        ssize_t res = read(c_fd, &message, sizeof(msg));
        if (res == 0) {
            printf("[The client disconnected.] \n");
            break;
        } else if (res == -1) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            printf(" Error on client socket:%s \n ", strerror(errno));
            break;
        }

        //Stuff to send back to the client
        switch (message.type){
            case GET:
            if(DBGet(dbFD, &message.rd) == -1){ // Get from server, failing if neccessary
                message.type=FAIL;
                write(c_fd, &message, sizeof(msg));

                continue;
            }
            message.type=SUCCESS; //Send back successful message

            write(c_fd, &message, sizeof(msg));
            break;

            case PUT:
            if(DBPut(dbFD, &message.rd) == -1){ // Put into server, failing if neccessary
                message.type=FAIL;
                write(c_fd, &message, sizeof(msg));
                continue;
            }
            message.type=SUCCESS; //Send back successful message
            write(c_fd, &message, sizeof(msg));
            break;

            default:
                break;
        }
    }

    close(c_fd);
    pthread_exit(NULL); //Close thread
}

int DBGet(int32_t fd, record* rd) { //Get from database
    size_t record_size = sizeof(record);
    int offs = lseek(fd, rd->id*sizeof(record), SEEK_SET);

    if(offs != rd->id*sizeof(record)) { //Make sure we got the right number of bytes
        return -1;
    }

    int nameRead = read(fd, rd, record_size); //Read from db
    if(nameRead < record_size)
      return -1;

    if(strcmp(rd->name, "") == 0 && rd->id == 0){ // If we are getting the values of a nonexistent database entry
        return -1;
    }

    return nameRead;
}

int DBPut(int32_t fd, record* rd) { // Put into database
    size_t record_size = sizeof(record);
    lseek(fd, rd->id*record_size, SEEK_SET);
    int nameWrite = write(fd, rd, record_size);
    if(nameWrite == -1){
      return -1;
    }
    return nameWrite;
}

void PrintRecord(record *rd) { // Print record
    printf("Record{ ");
    printf("name:[%s] ", rd->name);
    printf("id:[%d] }", rd->id);
}

void PrintMessage(msg* message){ //Print message
    printf("Message{ Type:[%d] ", message->type);
    PrintRecord(&message->rd);
    printf("}");
}