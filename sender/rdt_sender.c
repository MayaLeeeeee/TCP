#include "sender.h"
#include "../obj/packet.h"
#include "../obj/common.h"

int next_seqno=0;
int send_base=0;
int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;       


void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
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

// Function to simulate the packet processing at the receiver
void process_packet(Packet *pkt) {
    printf("Processed packet with sequence number: %d\n", pkt->seqNo);
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    Packet buffer[BUFFER_SIZE];
    int expectedSeqNo = 0;
    int i;

    // Initialize buffer
    for(i = 0; i < BUFFER_SIZE; i++) {
        buffer[i].seqNo = -1;  // Indicates an empty slot
    }

    // Bind the socket (assuming the details like port and IP are set on addr)
    bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));

    while(1) {
        Packet recvPkt;
        int bytesReceived = recvfrom(sockfd, &recvPkt, sizeof(recvPkt), 0, (struct sockaddr *)&addr, &addrlen);
        if(bytesReceived > 0) {
            // Check if the packet is the one we expect
            if(recvPkt.seqNo == expectedSeqNo) {
                process_packet(&recvPkt);
                expectedSeqNo++;
                
                // Check for any buffered packets that can now be processed
                while(buffer[expectedSeqNo % BUFFER_SIZE].seqNo == expectedSeqNo) {
                    process_packet(&buffer[expectedSeqNo % BUFFER_SIZE]);
                    buffer[expectedSeqNo % BUFFER_SIZE].seqNo = -1;  // Clear the slot
                    expectedSeqNo++;
                }
            } else if (recvPkt.seqNo > expectedSeqNo && recvPkt.seqNo < expectedSeqNo + BUFFER_SIZE) {
                // Buffer out-of-order packets
                int idx = recvPkt.seqNo % BUFFER_SIZE;
                if(buffer[idx].seqNo == -1) {  // Only buffer if the slot is empty
                    buffer[idx] = recvPkt;
                }
            }
            // Send cumulative ACK
            sendto(sockfd, &expectedSeqNo, sizeof(expectedSeqNo), 0, (struct sockaddr *)&addr, addrlen);
        }
    }
    return 0;
}
