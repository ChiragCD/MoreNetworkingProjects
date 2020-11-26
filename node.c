#include "q1.h"

int verbose, id, N, tot_size, curr_size, from;
char recvstring[100];
char sendstring[100];
int array[5000];

void send_num(int fd, int num, int forward) {
    if(verbose) {
        if(forward) printf("%d - Writing %d forward to %s\n", id, num, sendstring);
        else printf("%d - Writing %d back to %s\n", id, num, recvstring);
    }
    write(fd, (char *)&num, sizeof(num));
}

int get_num(int fd, int forward) {
    int num;
    int size = read(fd, (char *)&num, sizeof(num));
    if(verbose) {
        if(forward) printf("%d - Read %d from ahead %s\n", id, num, sendstring);
        else printf("%d - Read %d from behind %s\n", id, num, recvstring);
    }
    return num;
}

int get_dest(int id) {
    return ((id-1)%N) + 1;
}

int run_old(int id, int old, int new) {
    int receiver = get_num(old, 0);
    if(receiver == 0) {
        if(!new) return 0;
        send_num(new, 0, 1);
        return 0;
    }
    if(receiver != id) {
        if(!new) return 2;
        send_num(new, receiver, 1);
        send_num(new, get_num(old, 0), 1);
        int size = get_num(old, 0);
        send_num(new, size, 1);
        for(int i = 0; i < size; i++) send_num(new, get_num(old, 0), 1);
        return 1;
    }
    else {
        from = get_num(old, 0);
        int size = get_num(old, 0);
        for(int i = 0; i < size; i++) array[i] = get_num(old, 0);
        if(size == 1) {
            send_num(old, from, 0);
            send_num(old, id, 0);
            send_num(old, 1, 0);
            send_num(old, array[0], 0);
        }
        tot_size = size;
        curr_size = 1;
        while(1) {
            if(size == 1) break;
            send_num(new, get_dest(id+(size/2)), 1);
            send_num(new, id, 1);
            send_num(new, size - size/2, 1);
            for(int i = size/2; i < size; i++) send_num(new, array[i], 1);
            size = size/2;
        }
    }
}

int run_new(int id, int old, int new) {
    int receiver = get_num(new, 1);
    if(receiver != id) {
        send_num(old, receiver, 0);
        send_num(old, get_num(new, 1), 0);
        int size = get_num(new, 1);
        send_num(old, size, 0);
        for(int i = 0; i < size; i++) send_num(old, get_num(new, 1), 0);
        return 1;
    }
    else{
        get_num(new, 1);
        int size = get_num(new, 1);
        int temps[curr_size];
        for(int i = 0; i < curr_size; i++) temps[i] = array[i];
        int old_lim = curr_size;
        int count = 0;
        curr_size = 0;
        for(int i = 0; i < size; i++) {
            int num = get_num(new, 1);
            while(count < old_lim && temps[count] < num) array[curr_size++] = temps[count++];
            array[curr_size++] = num;
        }
        for(int i = count; i < old_lim; i++) array[curr_size++] = temps[i];
        if(curr_size == tot_size) {
            send_num(old, from, 0);
            send_num(old, id, 0);
            send_num(old, tot_size, 0);
            for(int i = 0; i < tot_size; i++) send_num(old, array[i], 0);
        }
    }
}

void run_node(int identity, int old, int new, int N_in, int v, char from[], char to[]) {
    verbose = v;
    id = identity;
    N = N_in;
    if(v) printf("In %d, from %s to %s\n", identity, from, to);
    strcpy(recvstring, from);
    strcpy(sendstring, to);

    struct pollfd ins[2];
    ins[0].fd = old;
    ins[1].fd = new;
    ins[0].events = POLLIN;
    ins[1].events = POLLIN;

    int status;
    while(1) {
        if(poll(ins, 2, -1) == -1) perror("poll");
        if(ins[0].revents & POLLIN) status = run_old(id, old, new);
        else if(ins[1].revents & POLLIN) status = run_new(id, old, new);
        else printf("%d - Polled unknown event\n", id);
        if(status == 0) break;
    }

    close(old);
    close(new);
    return;
}
