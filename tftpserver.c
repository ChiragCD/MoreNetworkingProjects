#include<fcntl.h>
#include<sys/socket.h>
#include<netdb.h>
#include<errno.h>
#include<sys/types.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<signal.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<sys/select.h>
#include<netinet/in_systm.h>
#include<netinet/ip.h>
#include<netinet/ip_icmp.h>
#include<netinet/udp.h>

#define MAX_CONNS 100
typedef struct status {
	int sock_fd;
	char file_name[4096];
	int last_chunk_sent;
	int num_retransmits;
        struct timeval time_last_sent;
        struct sockaddr_in client_address;
} status;

status statuses[MAX_CONNS];	//Max clients 100?

int get_time_diff (struct timeval t1, struct timeval t2) {
        return abs(t1.tv_sec - t2.tv_sec);
}

int max(int a, int b) {
        if(a > b) return a;
        return b;
}

int get(char file_name[4096], int chunk_num, char buf[512]) {
	FILE * f = fopen(file_name, "r");
        if(f==NULL) {
                perror("open");
                return -2;
        }
	fseek(f, 0L, SEEK_END);
	if(ftell(f) < ((chunk_num-1) * 512)) {fclose(f); return -1;}
	fseek(f, (long) ((chunk_num-1) * 512), SEEK_SET);
	for(int i = 0; i < 512; i++) if((buf[i] = fgetc(f)) == EOF) {fclose(f); return i;}
	fclose(f); return 512;
}

int send_data(int sock_fd, int received_ack) {
        // heloo
	int send_length = 512;
	char send_buf[516];
	char dummy[100];
	status * relevant;
	for(int i = 0; i < MAX_CONNS; i++) if(statuses[i].sock_fd == sock_fd) relevant = statuses+i;

	if(!received_ack && relevant->num_retransmits == 3) {
		// send error;
		// or don't bother
		return -2;	// remove from list of reads
	}

        send_buf[0] = 0;
        send_buf[1] = 3;
        send_buf[2] = (relevant->last_chunk_sent / 256);
        send_buf[3] = (relevant->last_chunk_sent % 256);

	if(!received_ack) {
		(relevant->num_retransmits)++;
		send_length = get(relevant->file_name, relevant->last_chunk_sent, send_buf+4);
                if(send_length == -2) {
                        send_buf[0] = 0;
                        send_buf[1] = 5;
                        send_buf[2] = 0;
                        send_buf[3] = 1;
                        strcpy(send_buf+4, "No such file exists or it could not be opened");
                        send(sock_fd, send_buf, strlen(send_buf+4)+5,0);		// To be done
                        return -1;
                }
                printf("Sending chunk %d of size %d to %s:%d\n", relevant->last_chunk_sent, send_length, inet_ntoa(relevant->client_address.sin_addr), ntohs(relevant->client_address.sin_port));
		send(sock_fd, send_buf, send_length+4,0);		// To be done
		return 0;
	}

	relevant->num_retransmits = 0;
	(relevant->last_chunk_sent)++;
        send_buf[2] = (relevant->last_chunk_sent / 256);
        send_buf[3] = (relevant->last_chunk_sent % 256);
	send_length = get(relevant->file_name, relevant->last_chunk_sent, send_buf+4);
        if(send_length == -2) {
                send_buf[0] = 0;
                send_buf[1] = 5;
                send_buf[2] = 0;
                send_buf[3] = 1;
                strcpy(send_buf+4, "No such file exists or it could not be opened");
                send(sock_fd, send_buf, strlen(send_buf+4)+5,0);		// To be done
                return -1;
        }
	if(send_length == -1) return 1;		// finished, remove from list of reads
	send(sock_fd, send_buf, send_length+4,0);		// To be done
        printf("Sending chunk %d of size %d to %s:%d\n", relevant->last_chunk_sent, send_length, inet_ntoa(relevant->client_address.sin_addr), ntohs(relevant->client_address.sin_port));
	return 0;
}

void start(int port) {
    int listenfd = socket( AF_INET, SOCK_DGRAM,0);
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(struct sockaddr_in));
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = htons(port);
    serv_addr.sin_family = AF_INET;
    if(bind(listenfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) <0)
        perror("bind");
    printf("Bound at %s:%d\n", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(listenfd, &rset);
    int maxfd = listenfd;
    for(int i=0;i<100;i++){
        statuses[i]. sock_fd = -1;
        statuses[i].last_chunk_sent = 0;
        statuses[i].num_retransmits = 0;
    }
    struct timeval tout1;
    tout1.tv_sec = 1 ;
    tout1.tv_usec = 0;
    while(1){
        int num_ready = select(maxfd + 1, &rset,NULL,NULL,&tout1);
        tout1.tv_sec = 1;
        if(FD_ISSET(listenfd, &rset)){
                printf("In listen\n");
                char rrq[100];
                int n_read;
                struct sockaddr_in cli_addr;
                int sz = sizeof(struct sockaddr_in);
                if((n_read = recvfrom(listenfd,rrq, 100, 0,(struct sockaddr*)&cli_addr, &sz)<0) )
                        {perror("read");continue;}
                if(rrq[1] != 1){ // in case opcode is not 2
                        printf("Invalid packet received\n");
                        printf(" The first byte has value %d and second has value %d\n", rrq[0], rrq[1]);
                        continue;
                }
                int file_len = strlen(rrq+2); // the file name starts from the second byte
                char filename[1024];
                strcpy(filename, rrq+2);
                char mode[100];
                strcpy(mode, rrq + file_len+3);
                printf("THe mode of transfer is %s\n", mode);
                if(strcmp(mode, "octet") && strcmp(mode, "binary")){ // check if the mode is supported
                        printf("The requested mode is %s but this server does not support it\n", mode);
                        FD_SET(listenfd, &rset);
                        continue;
                }
                // if we reach here, then it is a valid RRQ packet
                int free_stat;
                for(free_stat = 0;free_stat < MAX_CONNS ; free_stat++)
                        if(statuses[free_stat].sock_fd == -1)
                                break;
                int connfd = socket( AF_INET, SOCK_DGRAM, 0);
                statuses[free_stat].client_address = cli_addr;
                if(connect(connfd,(struct sockaddr *) &cli_addr, sizeof(struct sockaddr_in))<0)
                        perror("connect");
                FD_SET(connfd, &rset);
                maxfd = max(maxfd, connfd);
                statuses[free_stat].sock_fd = connfd;
                strcpy(statuses[free_stat].file_name,filename);
                statuses[free_stat].last_chunk_sent = 0;
                statuses[free_stat].num_retransmits = 0;
                int res = send_data(connfd, 1); 
                if(res == -1){
                        printf("File not found or could not be opened: %s\n",filename);
                        FD_CLR(connfd, &rset);
                        close(connfd);
                        statuses[free_stat].sock_fd = -1;
                        // continue;
                }
                struct timeval last_sent;
                gettimeofday(&last_sent, NULL);
                statuses[free_stat].time_last_sent = last_sent;
                // now we have an extra entry in statuses with the correct info
                // also connfd doesn't require sendto, send works 

        }
        else  // reset the listen fd 
                FD_SET(listenfd,&rset);

        for(int i = 0; i < MAX_CONNS; i++) {
                if(statuses[i].sock_fd == -1 )
                        continue;
                int sockfd = statuses[i].sock_fd;
                if(!FD_ISSET(statuses[i].sock_fd, &rset)){// could be that it has timed out
                        struct timeval cur, last_sent;
                        gettimeofday(&cur, NULL);
                        last_sent = statuses[i].time_last_sent;
                        int secs = get_time_diff(cur, last_sent);
                        if(secs >= 5){ // timed out
                                statuses[i].time_last_sent = cur;
                                int result = send_data(sockfd, 0);
                                if(result == -2) {
                                        printf("Timed out thrice while sending %s, giving up\n", statuses[i].file_name);
                                        FD_CLR(statuses[i].sock_fd, &rset);
                                        close(statuses[i].sock_fd);
                                        // close the thing
                                        statuses[i].last_chunk_sent = 0;
                                        statuses[i].num_retransmits = 0;
                                        statuses[i].sock_fd = -1;
                                }
                        }
                        else FD_SET(sockfd, &rset);
                        continue;
                }
                int result;
                char buffer[1000]; // read in the ack
                read(sockfd,buffer, 1000);
                printf("Received acknowledgement from %s:%d\n", inet_ntoa(statuses[i].client_address.sin_addr), ntohs(statuses[i].client_address.sin_port));
                result = send_data(sockfd, 1);
                struct timeval cur;
                gettimeofday(&cur, NULL);
                statuses[i].time_last_sent = cur;
                if(result == 1) {
                        printf("Finished sending %s\n", statuses[i].file_name);
                        close(statuses[i].sock_fd);
                        FD_CLR(statuses[i].sock_fd, &rset);
                        statuses[i].sock_fd = -1;
                }
                else FD_SET(sockfd, &rset);
        }
    }
}

int main(int argc, char * argv[]) {

	if(argc < 2) {
		printf("Proper usage: ./tftpserver <port number>\n");
		return 1;
	}
	int port = atoi(argv[1]);
	start(port);
	return 0;
}

