#ifndef RECEIVER_H
# define RECEIVER_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "../obj/common.h"
#include "../obj/packet.h"

#define WINDOW_SIZE 10

struct tcp_packet *packet_buffer[WINDOW_SIZE];


#endif