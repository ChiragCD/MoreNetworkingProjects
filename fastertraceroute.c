#include    <sys/socket.h>
#include    <pthread.h>
#include    <netdb.h>
#include    <errno.h>
#include    <sys/types.h>
#include    <stdio.h>
#include    <unistd.h>
#include    <string.h>
#include    <stdlib.h>
#include    <signal.h>
#include    <arpa/inet.h>
#include    <sys/time.h>
#include    <netinet/in_systm.h>
#include    <netinet/ip.h>
#include    <netinet/ip_icmp.h>
#include    <netinet/udp.h>
#define MAX_TTL 10

#define	BUFSIZE		1500


			/* globals */
char	 recvbuf[BUFSIZE];
char	 sendbuf[BUFSIZE];

int		 datalen;			/* # bytes of data following ICMP header */
char	*host;
u_short	 sport, dport;
int		 nsent;				/* add 1 for each sendto() */
pid_t	 pid;				/* our PID */
int		 probe, nprobes;
int		 sendfd;	/* send on UDP sock, read on raw ICMP sock */
int		 verbose;

const char	*icmpcode_v4(int);
void*		 icmp(void*);
void	 sig_alrm(int);
void	 traceloop(void);
void	 tv_sub(struct timeval *, struct timeval *);

struct proto {
  const char	*(*icmpcode)(int);
  void*	 (*recv)(void*);
  struct sockaddr  *sasend;	/* sockaddr{} for send, from getaddrinfo */
  struct sockaddr  *sarecv;	/* sockaddr{} for receiving */
  struct sockaddr  *salast;	/* last sockaddr{} for receiving */
  struct sockaddr  *sabind;	/* sockaddr{} for binding source port */
  socklen_t   		salen;	/* length of sockaddr{}s */
  int			icmpproto;	/* IPPROTO_xxx value for ICMP */
  int	   ttllevel;		/* setsockopt() level to set TTL */
  int	   ttloptname;		/* setsockopt() name to set TTL */
} *pr;

struct proto	proto_v4 = { icmpcode_v4, icmp, NULL, NULL, NULL, NULL, 0,IPPROTO_ICMP, IPPROTO_IP, IP_TTL };

pthread_mutex_t msg_received[MAX_TTL];
pthread_cond_t msg_signal[MAX_TTL];
struct sockaddr* recv_addresses[MAX_TTL];

int		nprobes = 1;
u_short	dport = 32768 + 666;
struct addrinfo * host_serv(const char *host, const char *serv, int family, int socktype)
{
        int n;
        struct addrinfo hints, *res;
        bzero (&hints, sizeof (struct addrinfo));
        hints.ai_flags = AI_CANONNAME; /* always return canonical name */
        hints.ai_family = family; /* AF_UNSPEC, AF_INET, AF_INET6, etc. */
        hints.ai_socktype = socktype; /* 0, SOCK_STREAM, SOCK_DGRAM, etc. */
        if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0)
            return (NULL);
        return (res);
}

void tv_sub(struct timeval *out, struct timeval *in)
{
	if ( (out->tv_usec -= in->tv_usec) < 0) {	/* out -= in */
		--out->tv_sec;
		out->tv_usec += 1000000;
	}
	out->tv_sec -= in->tv_sec;
}
const char *
icmpcode_v4(int code)
{
	static char errbuf[100];
	switch (code) {
    	case  0:	return("network unreachable");
    	case  1:	return("host unreachable");
    	case  2:	return("protocol unreachable");
    	case  3:	return("port unreachable");
    	case  4:	return("fragmentation required but DF bit set");
    	case  5:	return("source route failed");
    	case  6:	return("destination network unknown");
    	case  7:	return("destination host unknown");
    	case  8:	return("source host isolated (obsolete)");
    	case  9:	return("destination network administratively prohibited");
    	case 10:	return("destination host administratively prohibited");
    	case 11:	return("network unreachable for TOS");
    	case 12:	return("host unreachable for TOS");
    	case 13:	return("communication administratively prohibited by filtering");
    	case 14:	return("host recedence violation");
    	case 15:	return("precedence cutoff in effect");
    	default:	sprintf(errbuf, "[unknown code %d]", code);
    				return errbuf;
	}
}

const char* icmptype_v4(int type){
        switch(type){
                case 0 :return " echo reply";
                case 1 :return " unassigned";
                case 2 :return " unassigned";
                case 3 :return " destination unreachable";
                case 4 :return " source quench (deprecated)";
                case 5 :return " redirect";
                case 6 :return " alternate host address (deprecated)";
                case 7 :return " unassigned";
                case 8 :return " echo";
                case 9 :return " router advertisement";
                case 10 :return " router selection";
                case 11 :return " time exceeded";
                case 12 :return " parameter problem";
                case 13 :return " timestamp";
                case 14 :return " timestamp reply";
        }
}

void* icmp(void* arg)
{
	int				hlen1, hlen2, icmplen, ret;
	socklen_t		len;
	ssize_t			n;
	struct ip		*ip, *hip;
	struct icmp		*icmp;
	struct udphdr	*udp;

	int gotalarm = 0;
    struct timeval tout;
    tout.tv_sec = 10;
    tout.tv_usec = 0;
    int recvfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    //printf("created raw socket with fd = %d\n", recvfd);
    if(recvfd<0)
            perror("raw socket");
    if(setsockopt(recvfd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)))
            perror("setsockopt");

	for ( ; ; ) {
		if (gotalarm)
			break;		/* alarm expired */
		len = pr->salen;    // lenght of the structaddr_in
        //printf("attempting to recv from socketfd %d\n", recvfd);
		n = recvfrom(recvfd, recvbuf, sizeof(recvbuf), 0, pr->sarecv, &len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK){
                    gotalarm = 1;
                    continue;
            }
			else
            {perror("recvfrom error");exit(1);}
		}

		ip = (struct ip *) recvbuf;	/* start of ip header */
        // struct ip header matches the first 20 bytes received from recvbuf
        // the remaining should consist of the icmp header and the data returned (rec structure)
		hlen1 = ip->ip_hl << 2;		/* length of ip header */
		if ( (icmplen = n - hlen1) < 8)
			continue;				/* not enough to look at icmp header */
		icmp = (struct icmp *) (recvbuf + hlen1); /* start of icmp header */
        //printf("ICMP PACKET RECEIVED: TYPE = %s, CODE = %s\n", icmptype_v4(icmp->icmp_type), icmpcode_v4(icmp->icmp_code));
        if (icmp->icmp_type == ICMP_UNREACH) {
             if (icmplen < 8 + sizeof(struct ip))
                 continue;           /* not enough data to look at inner ip */
             hip = (struct ip *) (recvbuf + hlen1 + 8);
             hlen2 = hip->ip_hl << 2;
             if (icmplen < 8 + hlen2 + 4)
                 continue;           /* not enough data to look at udp ports */

             udp = (struct udphdr *) (recvbuf + hlen1 + 8 + hlen2);
             int idx = ntohs(udp->uh_dport)-dport;
            recv_addresses[idx] = malloc(pr->salen);
            *(recv_addresses[idx]) = *(pr->sarecv);
            pthread_cond_signal(&msg_signal[idx]);
            //printf("Signalled printing thread %d\n",idx);
            continue;
         }
        else if (icmp->icmp_type == ICMP_TIMXCEED && icmp->icmp_code == ICMP_TIMXCEED_INTRANS){  // check if error matches what we want
            if (icmplen < 8 + sizeof(struct ip))
                continue;			/* not enough data to look at inner IP */
            hip = (struct ip *) (recvbuf + hlen1 + 8);
            hlen2 = hip->ip_hl << 2;
            if (icmplen < 8 + hlen2 + 4)
                continue;			/* not enough data to look at UDP ports */

            udp = (struct udphdr *) (recvbuf + hlen1 + 8 + hlen2);
            int idx = ntohs(udp->uh_dport)-dport;
            pthread_mutex_lock(&msg_received[idx]);
            recv_addresses[idx] = malloc(pr->salen);
            *(recv_addresses[idx]) = *(pr->sarecv);
            pthread_mutex_unlock(&msg_received[idx]);
            pthread_cond_signal(&msg_signal[idx]);
            //printf("Signalled printing thread %d\n",idx);
        }
    }
}

void* trace(void* ttl_ptr)
{
	int					seq, code, done;
	double				rtt;
	struct timeval		tvrecv;
    int ttl = *((int*)ttl_ptr);
	sendfd = socket(pr->sasend->sa_family, SOCK_DGRAM, 0);   // create the UDP socket that sends the messages
    if(sendfd<0)
            perror("udp socket");
	pr->sabind->sa_family = AF_INET; // set the address family of the binding socket  as same as that of sending socket
	sport = (getpid() & 0xffff) | 0x8000;	/* our source UDP port # */
	((struct sockaddr_in*)(pr->sabind))->sin_port =  htons(sport); //set the local port as sport (send port) 
	bind(sendfd, pr->sabind, pr->salen); // finally bind the socket for sending with the details specified in sabind (sockaddr_in bind)
    setsockopt(sendfd, pr->ttllevel, pr->ttloptname, &ttl, sizeof(int)); // specify the ttl here
    bzero(pr->salast, pr->salen);
    ((struct sockaddr_in*)(pr->sasend))->sin_port = htons(dport + ttl);    // assign the sending socket port as dport + seq
    char sendbuf[10] = "HELLO";
    datalen = 6;
    //printf("Sending udp message\n");
    sendto(sendfd, sendbuf, datalen, 0, pr->sasend, pr->salen); // send udp msg
    if(pthread_mutex_lock(&msg_received[ttl]))
            perror("pthread mutex lock");
    if(recv_addresses[ttl] != NULL)
            ;
    else if(pthread_cond_wait(&msg_signal[ttl], &msg_received[ttl]))
            perror("pthread cond wait");
   // printf("Printing thread has received signal\n");
    char str[NI_MAXHOST];
    if (getnameinfo(recv_addresses[ttl], pr->salen, str, sizeof(str),NULL, 0, 0) == 0){
        char tmp[128];
        struct sockaddr_in* sa_tmp;
        sa_tmp = (struct sockaddr_in*)recv_addresses[ttl];
        inet_ntop(AF_INET,&(sa_tmp->sin_addr),tmp,sizeof(tmp));                      // convert the IP address to human readable format 
        printf("TTL %d:  %s (%s)\n",ttl, str, tmp);                                        
    }
    //printf("printing thread is about to send signal\n");
    if(pthread_mutex_unlock(&msg_received[ttl]))
            perror("mutex unlock");
    if(pthread_cond_signal(&msg_signal[ttl]))
            perror("cond signal");
    //printf("printing thread has sent signal\n");
}
int main(int argc, char* argv[]){
        struct addrinfo *ai;
        char h[100];
        if(argc!=2){printf("Usage: fastertraceroute <domain>\n"); exit(0);}
        host = argv[1];
        pid = getpid();
        ai = host_serv(host,NULL,AF_INET,0); // wrapper around getaddrinfo , only returns IPv4 socket structs
        char tmp[128];
        struct sockaddr_in* sa_tmp;
        sa_tmp = (struct sockaddr_in*)(ai->ai_addr);
        inet_ntop(AF_INET,&(sa_tmp->sin_addr),tmp,sizeof(tmp));
        inet_ntop(AF_INET, &(sa_tmp->sin_addr),h, sizeof(h));
        printf("traceroute to %s (%s): %d hops max, %d data bytes\n",
                        ai->ai_canonname ? ai->ai_canonname : h,
		                h, MAX_TTL, datalen);
        if (ai->ai_family == AF_INET) 
                pr = &proto_v4;
	    else{
                printf("unknown address family %d", ai->ai_family);
                return 0;
        }
        pr->sasend = ai->ai_addr;		/* contains destination address */
	    pr->sarecv = calloc(1, ai->ai_addrlen);
	    pr->salast = calloc(1, ai->ai_addrlen);
	    pr->sabind = calloc(1, ai->ai_addrlen);
	    pr->salen = ai->ai_addrlen;

        pthread_t tid;
        int ttl[MAX_TTL];
       // printf("creating threads\n");
        for(int i=0;i<MAX_TTL;i++){
                ttl[i] = i+1;
                if(pthread_mutex_init(&msg_received[i],NULL))
                        perror("mutex init");
                if(pthread_cond_init(&msg_signal[i],NULL))
                        perror("cond init");
        }
        for(int i=0;i<MAX_TTL; i++)
                pthread_create(&tid, NULL, trace,(void*) &ttl[i]);
        pthread_create(&tid, NULL, icmp, NULL);

        pthread_join(tid,NULL);
	    exit(0);
}

