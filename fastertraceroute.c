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

#define	BUFSIZE		1500

struct rec {					/* format of outgoing UDP data */
  u_short	rec_seq;			/* sequence number */
  u_short	rec_ttl;			/* TTL packet left with */
  struct timeval	rec_tv;		/* time packet left */
};

			/* globals */
char	 recvbuf[BUFSIZE];
char	 sendbuf[BUFSIZE];

int		 datalen;			/* # bytes of data following ICMP header */
char	*host;
u_short	 sport, dport;
int		 nsent;				/* add 1 for each sendto() */
pid_t	 pid;				/* our PID */
int		 probe, nprobes;
int		 sendfd, recvfd;	/* send on UDP sock, read on raw ICMP sock */
int		 ttl, max_ttl;
int		 verbose;

			/* function prototypes */
const char	*icmpcode_v4(int);
void*		 recv_v4(void*);
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


int gotalarm;
void sig_alrm(int signo)
{
    gotalarm = 1;
    return;
}
struct proto	proto_v4 = { icmpcode_v4, recv_v4, NULL, NULL, NULL, NULL, 0,IPPROTO_ICMP, IPPROTO_IP, IP_TTL };

int		datalen = sizeof(struct rec);	/* defaults */
int		max_ttl = 10;
int		nprobes = 1;
u_short	dport = 32768 + 666;
pthread_mutex_t send_lock; 
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

void* recv_v4(void* arg)
{
	int				hlen1, hlen2, icmplen, ret;
	socklen_t		len;
	ssize_t			n;
	struct ip		*ip, *hip;
	struct icmp		*icmp;
	struct udphdr	*udp;

	gotalarm = 0;
    struct timeval tout;
    tout.tv_sec = 10;
    tout.tv_usec = 0;
    setsockopt(recvfd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)); 

	for (int j=0 ;j<max_ttl*nprobes ; ) {
		if (gotalarm)
			break;		/* alarm expired */
		len = pr->salen;    // lenght of the structaddr_in
		n = recvfrom(recvfd, recvbuf, sizeof(recvbuf), 0, pr->sarecv, &len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK){
                    gotalarm = 1;
                    continue;
            }
			else
				perror("recvfrom error");
		}

		ip = (struct ip *) recvbuf;	/* start of IP header */
        // struct ip header matches the first 20 bytes received from recvbuf
        // the remaining should consist of the ICMP header and the data returned (rec structure)
		hlen1 = ip->ip_hl << 2;		/* length of IP header */
		if ( (icmplen = n - hlen1) < 8)
			continue;				/* not enough to look at ICMP header */
		icmp = (struct icmp *) (recvbuf + hlen1); /* start of ICMP header */
        if (icmp->icmp_type == ICMP_UNREACH) {
             if (icmplen < 8 + sizeof(struct ip))
                 continue;           /* not enough data to look at inner IP */
             hip = (struct ip *) (recvbuf + hlen1 + 8);
             hlen2 = hip->ip_hl << 2;
             if (icmplen < 8 + hlen2 + 4)
                 continue;           /* not enough data to look at UDP ports */

             udp = (struct udphdr *) (recvbuf + hlen1 + 8 + hlen2);
             char	str[NI_MAXHOST];
            if (getnameinfo(pr->sarecv, pr->salen, str, sizeof(str),NULL, 0, 0) == 0){
                char tmp[128];
                struct sockaddr_in* sa_tmp;
                sa_tmp = (struct sockaddr_in*)pr->sarecv;
                inet_ntop(AF_INET,&(sa_tmp->sin_addr),tmp,sizeof(tmp));                      // convert the IP address to human readable format 
                printf("TTL %d:  %s (%s)\n",ntohs(udp->uh_dport)-dport, str, tmp);                                        
            }
            continue;
         }

        // ip->ip_hl is the second field of the IP header it is of size 4 bits and contains the length of the ip header
        // actual length of the header is 4 * ip_hl becuase it works in increments of 32 bits, usually its value is 5 becuase usually IP header is 20 bytes long

		//if (icmp->icmp_type == ICMP_TIMXCEED &&	icmp->icmp_code == ICMP_TIMXCEED_INTRANS)  // check if error matches what we want
        if (icmplen < 8 + sizeof(struct ip))
            continue;			/* not enough data to look at inner IP */

        hip = (struct ip *) (recvbuf + hlen1 + 8);
        hlen2 = hip->ip_hl << 2;
        if (icmplen < 8 + hlen2 + 4)
            continue;			/* not enough data to look at UDP ports */

        udp = (struct udphdr *) (recvbuf + hlen1 + 8 + hlen2);
        struct rec *resp = (struct rec*) (recvbuf + hlen1 + hlen2 + 8 + sizeof(struct udphdr));
        // datalen is size of rec structure which contains seq, ttl, and timeval
        char	str[NI_MAXHOST];
        if (getnameinfo(pr->sarecv, pr->salen, str, sizeof(str),NULL, 0, 0) == 0){
            char tmp[128];
            struct sockaddr_in* sa_tmp;
            sa_tmp = (struct sockaddr_in*)pr->sarecv;
            inet_ntop(AF_INET,&(sa_tmp->sin_addr),tmp,sizeof(tmp));                      // convert the IP address to human readable format 
            printf("TTL %d:  %s (%s)\n",ntohs(udp->uh_dport)-dport, str, tmp);                                        
        }
}
}

void* trace(void* arg)
{
	int					seq, code, done;
	double				rtt;
	struct rec			*rec;
	struct timeval		tvrecv;

	recvfd = socket(pr->sasend->sa_family, SOCK_RAW, pr->icmpproto);  // create the raw socket for receiving the icmp messges
	setuid(getuid());		/* don't need special permissions anymore */


	sendfd = socket(pr->sasend->sa_family, SOCK_DGRAM, 0);   // create the UDP socket that sends the messages

	pr->sabind->sa_family = pr->sasend->sa_family; // set the address family of the binding socket  as same as that of sending socket
	sport = (getpid() & 0xffff) | 0x8000;	/* our source UDP port # */
	((struct sockaddr_in*)(pr->sabind))->sin_port =  htons(sport); //set the local port as sport (send port) 
	bind(sendfd, pr->sabind, pr->salen); // finally bind the socket for sending with the details specified in sabind (sockaddr_in bind)

    pthread_mutex_lock(&send_lock);
    ttl++;
    for(int i=0; i<nprobes;i++){
        rec = (struct rec *) sendbuf; // cast sendbuf (char*) to (struct rec*)
        rec->rec_seq = ++seq;         // increment the sequence number
        rec->rec_ttl = ttl;           // set the ttl
        gettimeofday(&rec->rec_tv, NULL); // set the current time
        setsockopt(sendfd, pr->ttllevel, pr->ttloptname, &ttl, sizeof(int)); // specify the ttl here
        bzero(pr->salast, pr->salen);
        ((struct sockaddr_in*)(pr->sasend))->sin_port = htons(dport + ttl);    // assign the sending socket port as dport + seq
        //sock_set_port(pr->sasend, pr->salen, htons(dport + seq));
        sendto(sendfd, sendbuf, datalen, 0, pr->sasend, pr->salen); // send udp msg
        pthread_mutex_unlock(&send_lock);
    }
}
int main(int argc, char* argv[]){
        struct addrinfo *ai;
        char h[100];
        if(argc!=2){printf("Usage: fastertraceroute <domain>\n"); exit(0);}
        host = argv[1];
        pid = getpid();
        signal(SIGALRM, sig_alrm); // not used, instead set timeout on socket itself
        ai = host_serv(host,NULL,AF_INET,0); // wrapper around getaddrinfo , only returns IPv4 socket structs
        char tmp[128];
        struct sockaddr_in* sa_tmp;
        sa_tmp = (struct sockaddr_in*)(ai->ai_addr);
        inet_ntop(AF_INET,&(sa_tmp->sin_addr),tmp,sizeof(tmp));
        inet_ntop(AF_INET, &(sa_tmp->sin_addr),h, sizeof(h));
        printf("traceroute to %s (%s): %d hops max, %d data bytes\n",
                        ai->ai_canonname ? ai->ai_canonname : h,
		                h, max_ttl, datalen);
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

	//traceloop();
    pthread_t tid;
    ttl = 0;
    for(int i=0;i<max_ttl; i++)
            pthread_create(&tid, NULL, trace, NULL);
    pthread_create(&tid, NULL, recv_v4, NULL);

    pthread_join(tid,NULL);
	exit(0);
}


