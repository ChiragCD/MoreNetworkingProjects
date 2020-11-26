#include "q1.h"

int verbose, N, sleeper;
int sorted[5000];
char from[100];
// char at[100];
char to[100];

void sendnum(int fd, int num) {
    if(verbose) printf("Coord - Writing to %s\n", to);
    write(fd, (char *)&num, sizeof(num));
}

int getnum(int fd) {
	int num;
    int size = read(fd, (char *)&num, sizeof(num));
    if(verbose) printf("Coord - Reading from %s\n", to);
    return num;
}

void run_coord (int new, int root, char in[], char out[]) {
	char buffer[10000];
	int infile;
	if((infile = open(in, O_RDONLY)) == -1) perror("Input file not found:");
	int length = read(infile, buffer, 10000);
	close(infile);
	if(length == 10000) printf("Input too large");
	// printf("Input - %s\n", buffer);
	sendnum(new, root);
	sendnum(new, 0);
	sendnum(new, N);
	
	char * token = strtok(buffer, " ");
	for(int i = 0; i < N; i++) {
		if(token == NULL) {
			printf("Error - insufficient input size. Please abort\n");
			for(int i = 0; i < N; i++) sendnum(new, 0);
			break;
		}
		sendnum(new, atoi(token));
		token = strtok(NULL, " ");
	}
	getnum(new);
	getnum(new);
	getnum(new);
	for(int i = 0; i < N; i++) sorted[i] = getnum(new);
	sendnum(new, 0);

	chmod(out, 0644);
	FILE * outfile = fopen(out, "w");
	if(outfile == NULL) perror("Output file not opened:");
	char buf[100];
	for(int i = 0; i < N; i++) fprintf(outfile, "%d ", sorted[i]);
	fclose(outfile);
}

void start(int root, char in[], char out[]) {

	int ports[N];
	root -= 1;
	pid_t coord = getpid();

	struct sockaddr_in binder, address;
    socklen_t addr_size = sizeof(binder);

	int old, new, listener;
	new = 0;

	bzero(&binder, sizeof(binder));
    binder.sin_family = AF_INET;
    binder.sin_addr.s_addr = INADDR_ANY;
    binder.sin_port = 0;

	int i;
	for(i = 0; i < N; i++) {
		listener = socket(AF_INET, SOCK_STREAM, 0);
		if(bind(listener, (struct sockaddr *)&binder, sizeof(binder))) perror("Bind new:");
		getsockname(listener, (struct sockaddr *)&address, &addr_size);
		pid_t id = fork();
		if(id != 0) {
			if(listen(listener, 1)) perror("listen new:");
			// sprintf(at, "%s:%d", inet_ntoa(address.sin_addr), address.sin_port);
			if((new = accept(listener, (struct sockaddr *)&address, &addr_size)) == -1) perror("accept new:");
			close(listener);
			sprintf(to, "%s:%d", inet_ntoa(address.sin_addr), address.sin_port);
			break;
		}
		if(id == 0) {
			close(listener);
			sprintf(from, "%s:%d", inet_ntoa(address.sin_addr), address.sin_port);
			old = socket(AF_INET, SOCK_STREAM, 0);
			if(bind(old, (struct sockaddr *)&binder, sizeof(binder))) perror("Bind old:");
			if(connect(old, (struct sockaddr *)&address, addr_size)) perror("Connect:");
		}
	}

	if(getpid() == coord) {
		sleep(sleeper);
		root += 1;
		// printf("Coord at %s\n", at);
		run_coord(new, root, in, out);
		sleep(sleeper);
		close(new);
		for(int  i =0; i < N; i++) wait(NULL);
	}
	else {
		run_node((root+i-1)%N+1, old, new, N, verbose, from, to);
		if(i != N-1) close(new);
		close(old);
	}
}

int main(int argc, char * argv[]) {

	if(argc < 3) {
		printf("Proper usage: ./coordinator <N> <Root> <Verbose?(0/1)> <optional in file> <optional out file>\nN is number of nodes\nRoot is base node\n");
		return 1;
	}
	char in[100];
	char out[100];
	N = atoi(argv[1]);
	sleeper = N/128 + 1;
	int root = atoi(argv[2]);
	verbose = 1;
	getcwd(in, 100);
	strcpy(out, in);
	strcat(in, "/in");
	strcat(out, "/out");
	if(argc > 3) verbose = atoi(argv[3]);
	if(argc > 4) strcpy(in, argv[4]);
	if(argc > 5) strcpy(out, argv[5]);
	start(root, in, out);
	return 0;
}
