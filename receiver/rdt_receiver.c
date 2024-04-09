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

#include "../common.h"
#include "../packet.h"


/*
 * You are required to change the implementation to support
 * window size greater than one.
 * In the current implementation the window size is one, hence we have
 * only one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *sndpkt;

int next_seqno = 0;
int window_size = 10;
int cur_buffer_size = 0;

struct tcp_packet packet_buffer[window_size];

void append_buffer(struct tcp_packet *pkt)
{
    packet_buffer[cur_buffer_size] = pkt;
    cur_buffer_size++;
}

void pop_buffer(struct tcp_packet *pkt)
{
    // int buffer_len = sizeof(packet_buffer) / sizeof(packet_buffer[0]);
    for (int i = 0; i < cur_buffer_size; i++) 
    {
        if (pakcet_buffer[i]->hdr.seqno == pkt->hdr.seqno)
        {
            // shift elements to fill the gap
            for (int j = i; j < cur_buffer_size - 1; j++) {
                packet_buffer[j] = packet_buffer[j + 1];
            }
            cur_buffer_size--; // decrement cur_buffer_id if it's greater than 0
            break; // exit the loop after removing the element
        }
    }
}

void send_ack(int sockfd, struct sockaddr_in *clientaddr)
{
    sndpkt = make_packet(0);
    sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
    sndpkt->hdr.ctr_flags = ACK;
    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
            (struct sockaddr *) &clientaddr, clientlen) < 0) {
        error("ERROR in sendto");
    }
    next_seqno = sndpkt->hdr.ackno + 1;
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        if (recvpkt->hdr.seqno == next_seqno)
        {
            if ( recvpkt->hdr.data_size == 0) {
                //VLOG(INFO, "End Of File has been reached");
                fclose(fp);
                break;
            }
            /* 
            * sendto: ACK back to the client 
            */
            gettimeofday(&tp, NULL);
            VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);

            send_ack(sockfd, &clientaddr);
        }
        else if (recvpkt->hdr.seqno > next_seqno) // a later packet is received before the one we're waiting for currently
        {
            append_buffer(recvpkt);
        }

    }

    return 0;
}
