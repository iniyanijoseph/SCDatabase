#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "msg.h"

#define BUFFSIZE 4096
#define UNDEFINED 0
#define EXIT 0
#define true 1
#define false 0
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef i8 bool;
typedef struct sockaddr SA;
typedef struct msg MSG;
typedef struct record RD;

void fail(const char *fmt, ...);
bool mayfail(int result, int expect);
void Usage(char *progname);
int LookupName(char *name, unsigned short port, struct sockaddr_storage *ret_addr, size_t *ret_addr_size);
int Connect(struct sockaddr_storage *server_addr, size_t server_addr_len, int *server_fd);
void startUserShell(int server_fd);
int waitForResponse(int server_fd, MSG *response);
bool isInteger(char* number);
char *getInput();
void PrintRecord(RD *record);
void PrintMessage(MSG* message);

int main(int argc, char* argv[]) {
    if (argc != 3) {
        Usage(argv[0]);
    }

    // Step 1) Get IP address and port
    i16 port;
    mayfail(sscanf(argv[2], "%hu", &port), 2); // != 1

    struct sockaddr_storage server_addr;
    size_t server_addr_len;
    mayfail(LookupName(argv[1], port, &server_addr, &server_addr_len), 0);

    // Step 2 + 3) Create socket + Connect to server
    int server_fd;
    mayfail(Connect(&server_addr, server_addr_len, &server_fd), 0);

    // Step 4) use read(server_fd) and write(server_fd)
    startUserShell(server_fd);
    
    // Step 5) Close(server_fd)
    printf("Thanks for using dbcilent\n");
    close(server_fd);
    return 0;
}

void Usage(char *progname) {
    printf("usage: %s hostname port \n", progname);
    exit(1);
}

void fail(const char *fmt, ...) {
    int errno_save;
    va_list ap;
    errno_save = errno;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);

    if (errno_save != 0) {
        fprintf(stdout, "(errno = %d) : %s\n", errno_save, strerror(errno_save));
        fprintf(stdout, "\n");
        fflush(stdout);
    }
    va_end(ap);
    exit(1);
}

bool mayfail(int result, int expect) {
    return result >= expect;
}

int LookupName(char *name, unsigned short port, struct sockaddr_storage *ret_addr, size_t *ret_addr_size) {
    struct addrinfo hints, *results;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int retval;
    if ((retval = getaddrinfo(name, NULL, &hints, &results)) != 0) {
        printf("getaddrinfo failed: %s", gai_strerror(retval));
        exit(1);
    }

    if (results->ai_family == AF_INET) {
        struct sockaddr_in *v4addr = (struct sockaddr_in *) (results->ai_addr);
        v4addr->sin_port = htons(port);
    } else if (results->ai_family == AF_INET6) {
        struct sockaddr_in6 *v6addr = (struct sockaddr_in6 *)(results->ai_addr);
        v6addr->sin6_port = htons(port);
    } else {
        printf("getaddrinfo failed to provide an IPv4 or IPv6 address \n");
        freeaddrinfo(results);
        return 1;
    }

    // Return the first result.
    assert(results != NULL);
    memcpy(ret_addr, results->ai_addr, results->ai_addrlen);
    *ret_addr_size = results->ai_addrlen;

    freeaddrinfo(results);
    return 0;
}

int Connect(struct sockaddr_storage *server_addr, size_t server_addr_len, int *server_fd) {
    // Step 2 create the socket
    int socket_fd;
    mayfail((socket_fd = socket(server_addr->ss_family, SOCK_STREAM, 0)), 0);

    // Step 3 connect the socket 
    mayfail(connect(socket_fd, (SA*)server_addr, server_addr_len), 0);
    *server_fd = socket_fd; 
    return 0;
}

void startUserShell(int server_fd) {
    bool running = true;
    char *inputString = "";
    int inputInt;
    while (running) {
        while (!isInteger(inputString)) {
            printf("Enter your choice (1 to put, 2 to get, 0 to quit): ");
            inputString = getInput();
            inputInt = atoi(inputString);
            if (!isInteger(inputString)) {
                printf("You entered [%s]. Input must be either 0, 1, or 2\n", inputString);
                free(inputString);
            }
        }
        free(inputString);

        MSG request = {0};
        MSG response = {0};
        RD record = {0};
        size_t nbytes;
        switch (inputInt) {
        case EXIT:
            printf("You entered EXIT\n");
            running = false;
            break;
        case PUT:
            request.type = PUT;
            printf("Enter the name: ");
            char *name = getInput();
            strcpy(record.name, name);
            free(name);

            printf("Enter the id: ");
            char *IDstr = getInput();
            record.id = atoi(IDstr); // no input validation for now
            request.rd = record;
            free(IDstr);

            request.rd = record;
            nbytes = write(server_fd, &request, sizeof(MSG)); // write may fail

            // printf("Request ");
            // PrintMessage(&request);
            // printf("\n");

            waitForResponse(server_fd, &response);
            switch (response.type) {
            case SUCCESS:
                printf("put success.\n");
                break;
            case FAIL:
                printf("put failed.\n");
                break;
            default:
                printf("ERROR: unknown reponse type\n");
                break;
            }
            break;
        case GET:
            request.type = GET;
            printf("Enter the id: ");
            IDstr = getInput();
            record.id = atoi(IDstr);
            free(IDstr);

            request.rd = record;
            nbytes = write(server_fd, &request, sizeof(MSG)); // write may fail
            waitForResponse(server_fd, &response);

            // printf("Request ");
            // PrintMessage(&request);
            // printf("\n");

            switch (response.type) {
            case SUCCESS:
                printf("name: %s\n", response.rd.name);
                printf("id: %d\n", response.rd.id);
                break;
            case FAIL:
                printf("get failed.\n");
                break;
            default:
                break;
            }
            break;
        default:
            printf("You entered [%d]. Input must be either 0, 1, or 2\n", inputInt);
            break;
        }
    }
}

bool isInteger(char* number) {
    if (number[0] == '\0') {
        return false;
    }
    for (int i = 0; number[i] != '\0'; i++) {
        if (!isdigit(number[i])) {
            return false;
        }
    }
    return true;
}

char *getInput() {
    size_t bufferSize = BUFFSIZE;
    char *line = (char*)malloc(sizeof(char) * bufferSize);
    ssize_t bytesRead;
    if ((bytesRead = getline(&line, &bufferSize, stdin)) == -1) {
        printf("This is a non fatal error!\n");
    }
    line[bytesRead - 1] = '\0';
    return line;
}

bool isValidPortString(char *portStr) {
    if (!isInteger(portStr)) {
        return false;
    }
    i32 port = atoi(portStr);
    return 0 < port && port <= USHRT_MAX;
}

int waitForResponse(int server_fd, MSG *response) {
    printf("Waiting on a message from the server...\n");
    int status = UNDEFINED;
    while (true) {
        ssize_t result = read(server_fd, response, sizeof(MSG));
        if (result == 0) {
            status = response->type;
            printf("server has disconnected\n");
            break;
        } else if (result < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                continue;
            }
            printf("Error on client socket [%s]\n", strerror(errno));
            break;
        } else if (result > 0) {
            printf("Response type: [%d]\n", response->type);
            break;
        }
    }
    return status;
}


void PrintRecord(RD *record) {
    printf("Record{ ");
    printf("name:[%s] ", record->name);
    printf("id:[%d] }", record->id);
}

void PrintMessage(MSG* message){
    printf("Message{ Type:[%d] ", message->type);
    PrintRecord(&message->rd);
    printf(" }");
}