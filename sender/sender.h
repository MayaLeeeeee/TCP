#ifndef SENDER_H
# define SENDER_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include "../obj/packet.h"
#include "../obj/common.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond
#define MAX_SEQ 256
#define BUFFER_SIZE 10  // As per window size
#define PACKET_SIZE 1024

typedef struct {
    int seqNo;
    int size;
    char data[PACKET_SIZE];
} Packet;


#endif