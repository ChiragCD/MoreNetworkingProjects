#ifndef Q1_H
#define Q1_H

#include <stdlib.h>
#include <stdio.h>

#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>

#include <sys/stat.h>

void run_node(int id, int old, int new, int N, int v, char from[], char to[]);

#endif