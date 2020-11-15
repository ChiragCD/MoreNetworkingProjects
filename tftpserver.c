#include <stdlib.h>
#include <stdio.h>

typedef struct status {
	int sock_fd;
	//address;
	char file_name[4096];
	int last_chunk_sent;
	int num_retransmits;
} status;

status statuses[100];	//Max clients 100?

int get(char file_name[4096], int chunk_num, char buf[512]) {
	FILE * f = fopen(file_name, "r");
	fseek(f, 0L, SEEK_END);
	if(ftell(f) < ((chunk_num-1) * 512)) {fclose(f); return -1;}
	fseek(f, (long) ((chunk_num-1) * 512), SEEK_SET);
	for(int i = 0; i < 512; i++) if((buf[i] = fgetc(f)) == EOF) {fclose(f); return i;}
	fclose(f); return 512;
}

int send_data(int sock_fd, int received_ack) {

	int send_length = 512;
	char send_buf[516];
	char dummy[100];
	status * relevant;
	for(int i = 0; i < 100; i++) if(statuses[i].sock_fd == sock_fd) relevant = statuses+i;

	if(!received_ack && relevant->num_retransmits == 3) {
		// send error;
		// or don't bother
		return -2;	// remove from list of reads
	}

	((short *) send_buf)[0] = (short) 3;		// Data send
	((short *) send_buf)[1] = (short) relevant->last_chunk_sent;

	if(!received_ack) {
		(relevant->num_retransmits)++;
		send_length = get(relevant->file_name, relevant->last_chunk_sent, send_buf+4);
		sendto(sock_fd, relevant->address, send_buf, send_length+4);		// To be done
		recvfrom(sock_fd, relevant->address, dummy, 100, NON_BLOCK);		// To be done
		return 0;
	}

	relevant->num_retransmits = 0;
	(relevant->last_chunk_sent)++;
	send_length = get(relevant->file_name, relevant->last_chunk_sent, send_buf+4);
	if(send_length == -1) return 1;		// finished, remove from list of reads
	sendto(sock_fd, relevant->address, send_buf, send_length+4);		// To be done
	recvfrom(sock_fd, relevant->address, dummy, 100, NON_BLOCK);		// To be done
	return 0;
}

void start(int port) {
	printf("Hello World!\n");
}

int main(int argc, char * argv[]) {

	if(argc < 1) {
		printf("Proper usage: ./tftpserver <port number>\n");
		return 1;
	}
	int port = atoi(argv[1]);
	start(port);
	return 0;
}
