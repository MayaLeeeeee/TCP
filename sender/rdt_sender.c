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
#include <math.h>

#include"../obj/packet.h"
#include"../obj/common.h"

#define STDIN_FD    0
#define RETRY  1200 //millisecond
// #define RTO_MAX 240000

#define MAX_RETRANSMISSIONS 5
#define INITIAL_RTO 3000
#define MAX_RTO 240000
#define ALPHA 0.125
#define BETA 0.25
#define BUF_SIZE 1024

// int rtt = 120;
// int rttvar = 60;
// int rto = 3000;
// float alpha = 0.125;
// float beta = 0.25;
// FILE *cwnd_csv;

// float cwnd = 1.0;
// int ssthresh = 64;
// bool in_slow_start = true;

int next_seqno=0;
int send_base=0;
// int window_size = cwnd;
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
tcp_packet	*packetBufer[256];

float elapsed_time = 0.0;

// char buffer[BUF_SIZE];
struct timeval start, end;
double estimated_rtt = 0.0, dev_rtt = 0.0;
double rto = INITIAL_RTO;
int retransmissions = 0;

// Congestion control variables
double cwnd = 1.0;
int ssthresh = 64;
// int dup_acks = 0;
int last_ack = -1;
bool slow_start = true;

FILE *csv_file;



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
	while (next_seqno < send_base + cwnd)
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

bool initialize_csv_logging(const char *csv_path) {
    csv_file = fopen(csv_path, "w");
    if (!csv_file) {
        perror("fopen()");
        return false;
    }
    // fprintf(csv_file, "cwnd,ssthresh,rto,retransmissions\n");
    fflush(csv_file);
    return true;
}

// void record_csv_logging(double cwnd, int ssthresh, double rto, int retransmissions) {
//     fprintf(csv_file, "%d,%d,%d,%d\n", (int)cwnd, ssthresh, (int)rto, retransmissions);
//     fflush(csv_file);
// }

void record_csv_logging(float elapsed_time, double cwnd, int ssthresh) {
    fprintf(csv_file, "%f,%f,%d\n", elapsed_time, cwnd, ssthresh);
    fflush(csv_file);
}

void close_csv_logging() {
    if (csv_file) {
        fclose(csv_file);
    }
}

// Function to calculate the retransmission timeout (RTO)
void calculate_rto(double sample_rtt) {
    if (estimated_rtt == 0.0) {
        // First measurement
        estimated_rtt = sample_rtt;
        dev_rtt = sample_rtt / 2.0;
    } else {
        estimated_rtt = (1 - ALPHA) * estimated_rtt + ALPHA * sample_rtt;
        dev_rtt = (1 - BETA) * dev_rtt + BETA * fabs(sample_rtt - estimated_rtt);
    }
    rto = estimated_rtt + 4 * dev_rtt;

    // Ensure RTO is within bounds
    if (rto < 1) rto = 1;
    if (rto > MAX_RTO) rto = MAX_RTO;
}

// Function to adjust the congestion window
void adjust_cwnd(int ack_num) {
    // record_csv_logging(elapsed_time, cwnd, ssthresh);

    if (ack_num == last_ack) {
        // dupAck_count++;
        if (dupAck_count == 3) {
            // Fast Retransmit
            ssthresh = fmax(cwnd / 2, 2);
            cwnd = 1;
            dupAck_count = 0;
            slow_start = true;
            fprintf(stderr, "Fast Retransmit triggered: ssthresh = %d, cwnd = %f\n", ssthresh, cwnd);
        }
    } else {
        dupAck_count = 0;
        last_ack = ack_num;

        if (slow_start) {
            cwnd += 1.0;
            if (cwnd >= ssthresh) {
                slow_start = false;
                fprintf(stderr, "Switching to Congestion Avoidance\n");
            }
        } else {
            cwnd += 1.0 / cwnd;
        }
    }
}


int main (int argc, char **argv)
{
    int portno, len;
    char *hostname;
    char buffer[DATA_SIZE];
    // FILE *file_path;
    struct timeval start_time, cur_time;


    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    // file_path = fopen(argv[3], "rb");
    const char *file_path = argv[3];

    if (file_path == NULL) {
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

    init_timer(rto, resend_packets);
	next_expected_ack = send_base + 1;

    // Open file to send
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("fopen()");
        close(sockfd);
        exit(1);
    }

    // Initialize CSV logging
    const char *csv_path = "CWND.csv";
    if (!initialize_csv_logging(csv_path)) {
        fclose(file);
        close(sockfd);
        exit(1);
    }

    gettimeofday(&start_time, 0);

    while (1)
    {
        // time (&rawtime);
        // timeinfo = localtime (&rawtime);
        // printf ( "Current local time and date: %s", asctime (timeinfo) );
        // fprintf(cwnd_csv, "%s, CWND: %d", asctime(timeinfo), cwnd);
        gettimeofday(&cur_time, 0);
        elapsed_time = fabs((cur_time.tv_sec - start_time.tv_sec) * 1000.0 + (cur_time.tv_usec - start_time.tv_usec) / 1000.0); 

        record_csv_logging(elapsed_time, cwnd, ssthresh);

		for (int i = 0; i < cwnd; i++)
		{
        	len = fread(buffer, 1, DATA_SIZE, file);
        	if (len <= 0)
        	{
        	    VLOG(INFO, "End Of File has been reached");
        	    sndpkt = make_packet(0);
				packetBufer[i] = sndpkt;
        	    break;
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

			for (int i = 0; i < cwnd; i++)
			{
				if (packetBufer[i]->hdr.data_size == 0)
					return ;
            	if(sendto(sockfd, packetBufer[i], TCP_HDR_SIZE + get_data_size(packetBufer[i]), 0, 
            	            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            	{
            	    error("sendto");
            	}
				printf("sent packet with seqno %d\n", packetBufer[i]->hdr.seqno);
			}
            start_timer();
            gettimeofday(&start, NULL);

            // Wait for acknowledgment
            // char ack_buf[BUF_SIZE];
            struct timeval timeout;
            timeout.tv_sec = (int)rto;
            timeout.tv_usec = (int)((rto - timeout.tv_sec) * 1000000);


            do
            {
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                assert(get_data_size(recvpkt) <= DATA_SIZE);

				if (recvpkt->hdr.ackno == prev_ack)
				{
					dupAck_count++;
                    retransmissions++;
                    
                    // Exponential backoff
                    rto *= 2;
                    if (rto > MAX_RTO) rto = MAX_RTO;
				}
				else
                {
                    int ack_num = atoi(buffer);
                    gettimeofday(&end, NULL);
                    double sample_rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
                    calculate_rto(sample_rtt);
                    adjust_cwnd(ack_num);
					dupAck_count = 0;
                    retransmissions = 0;
                }
				if (recvpkt->hdr.ackno > send_base)
					send_base = recvpkt->hdr.ackno;// move window forward

				//handle retransmission upon timeout or 3 duplicates
				else if (timeout_occured || dupAck_count == 3)
                {
                    // fast retransmit, enter slow start
                    int ack_num = atoi(buffer);
                    adjust_cwnd(ack_num);
					resend_packets(SIGALRM);
                }

				else if (recvpkt->hdr.ackno == next_expected_ack)
					next_expected_ack++;
				
				prev_ack = recvpkt->hdr.ackno;

            }while(recvpkt->hdr.ackno < next_seqno && send_base != next_seqno);    //ignore duplicate ACKs
            stop_timer();
            // update_cwnd(cwnd+1);
        } while(recvpkt->hdr.ackno != next_seqno);

        free(sndpkt);
    }
	
    close_csv_logging();
    return 0;

}



