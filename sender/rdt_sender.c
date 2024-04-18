#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"../obj/packet.h"
#include"../obj/common.h"

#define STDIN_FD    0
#define RETRY  1200 //millisecond

int next_seqno=0;
int send_base=0;
int window_size = 10;
int dupAck_count = 0;
int	prev_ack = -1;
int next_expected_ack;
bool timeout_occured = false;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;
tcp_packet	*packetBufer[10];


void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
		timeout_occured = true;
		VLOG(INFO, "Resending Packets");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
	dupAck_count = 0;
}

void	send_packet(void)
{
	while (next_seqno < send_base + window_size)
	{
		sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, (const struct sockaddr *)&serveraddr, serverlen);
		next_seqno++;
	}
}


void	receive_ack(void)
{
	char	buffer[1024];

	int	length = recvfrom(sockfd, &recvpkt, sizeof(recvpkt), 0, NULL, NULL);
	if (length > 0)
	{
		stop_timer();
		printf("ACK received (number: %d)\n", recvpkt->hdr.ackno);
	}
	else
		perror("recvfrom");
}


void start_timer()
{
	timeout_occured = false;
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
	timeout_occured = false;
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "rb");

    if (fp == NULL) {
        error(argv[3]);
    }

    //socket: create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    //initialize server server details
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    //covert host into network byte order
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    //build the server's Internet address
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
	next_expected_ack = send_base + 1;

    while (1)
    {
		for (int i = 0; i < window_size; i++)
		{
        	len = fread(buffer, 1, DATA_SIZE, fp);
        	if ( len <= 0)
        	{
        	    VLOG(INFO, "End Of File has been reached");
        	    sndpkt = make_packet(0);
				printf("sndpkt seqno updated to %d\n", sndpkt->hdr.seqno);
        	    sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
        	            (const struct sockaddr *)&serveraddr, serverlen);
        	    //break;
        	}
			else
			{
        		send_base = next_seqno;
        		next_seqno = send_base + len;
        		sndpkt = make_packet(len);
        		memcpy(sndpkt->data, buffer, len);
        		sndpkt->hdr.seqno = send_base;
				packetBufer[i] = sndpkt;
			}
		}
        //Wait for ACK
        do {

			for (int i = 0; i < window_size; i++)
			{
            	/* VLOG(DEBUG, "Sending packet %d to %s", 
            	        send_base, inet_ntoa(serveraddr.sin_addr)); */
            
            	// If the sendto is called for the first time, the system will
            	// will assign a random port number so that server can send its
            	// response to the src port.
            
            	if(sendto(sockfd, packetBufer[i], TCP_HDR_SIZE + get_data_size(packetBufer[i]), 0, 
            	            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            	{
            	    error("sendto");
            	}
				printf("sent packet with seqno %d\n", packetBufer[i]->hdr.seqno);
			}
            start_timer();

            do
            {
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                printf("get data size(recvpkt): %d \n", get_data_size(recvpkt));
                assert(get_data_size(recvpkt) <= DATA_SIZE);

				printf("\tprev ack: %d\n", prev_ack);
				printf("\tack number: %d\n", recvpkt->hdr.ackno);
				
				// process based on ack number
				//ADD CODE TO PROCESS BASED ON ACK NUMBER HERE
				printf("send base: %d\n", send_base);
				printf("next seqno: %d\n", next_seqno);
				if (recvpkt->hdr.ackno == prev_ack)
				{
					printf("\t\tgot dup acks\n");
					dupAck_count++;
				}
				else
					dupAck_count = 0;
				if (recvpkt->hdr.ackno > send_base)
					send_base = recvpkt->hdr.ackno;// move window forward

				//handle retransmission upon timeout or 3 duplicates
				else if (timeout_occured || dupAck_count == 3)
					resend_packets(SIGALRM);

				else if (recvpkt->hdr.ackno == next_expected_ack)
					next_expected_ack++;
				
				prev_ack = recvpkt->hdr.ackno;

            }while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
            stop_timer();
        } while(recvpkt->hdr.ackno != next_seqno);      

        free(sndpkt);
    }
	

    return 0;

}



