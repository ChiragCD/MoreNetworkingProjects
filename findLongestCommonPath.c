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

#define MAX_TTL 20
#define START_PORT 60000
typedef struct sockaddr SA;
int path_ips[500];  // used to store all the values read from ICMP packets
struct addrinfo* Getaddrinfo( char* url){ // special lookup for this program
    struct addrinfo hint , *result;
    bzero(&hint, sizeof(struct addrinfo));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_flags = AI_CANONNAME;
    int err;
    err = getaddrinfo(url, NULL, &hint, &result);
    if (err)
    {
        if (err == EAI_SYSTEM)
            fprintf(stderr, "looking up %s: %s\n",url, strerror(errno));
        else
            fprintf(stderr, "looking up %s: %s\n",url, gai_strerror(err));
        exit(1);
    }
    return result;
}
void printaddrinfo(struct addrinfo *res){ 
    char addrstr[100];
    while (res)
    {
      void* ptr;
      inet_ntop (res->ai_family, res->ai_addr->sa_data, addrstr, 100);
      ptr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
      inet_ntop (res->ai_family, ptr, addrstr, 100);
      printf ("IPv4 address: %s (%s)\n",addrstr, res->ai_canonname);
      res = res->ai_next;
    }
}

void proc_icmp(char* recvbuf, int n){
         struct ip *ip, *hip; /* start of IP header */
         struct udphdr *udp;
         struct icmp *icmp;
         int hlen1, hlen2, icmplen;
         ip = (struct ip *) recvbuf;
         hlen1 = ip->ip_hl << 2;     /* length of IP header */

         icmp = (struct icmp *) (recvbuf + hlen1); /* start of ICMP header */
         if ( (icmplen = n - hlen1) < 8)
             return;               /* not enough to look at ICMP header */

        if (icmplen < 8 + sizeof(struct ip))
            return;           /* not enough data to look at inner IP */

        hip = (struct ip *) (recvbuf + hlen1 + 8);
        hlen2 = hip->ip_hl << 2;
        if (icmplen < 8 + hlen2 + 4)
            return;           /* not enough data to look at UDP ports */

        udp = (struct udphdr *) (recvbuf + hlen1 + 8 + hlen2);
        if (hip->ip_p != IPPROTO_UDP)
            return;
        if(ntohs(udp->uh_dport) >= START_PORT)
            path_ips[ntohs(udp->uh_dport)-START_PORT] = ip->ip_src.s_addr;
}
int max(int a , int b){
        return a>b?a:b;
}

void printsockfd(int sockfd){
        struct sockaddr_in res;
        int sz;
        sz = sizeof(res);
        getpeername(sockfd,(SA*) &res,&sz);
        printf("looking at sockfd %d, it has port %u and ip %u\n", sockfd, ntohs(res.sin_port), ntohl(res.sin_addr.s_addr));
        return;
}
        

int main(int argc, char* argv[]){
    if(argc !=2 ){
            printf("Usage: findLongestCommonPath domains.txt\n"); 
            exit(1);
    }
    char domain_names[10][200];
    char buff[200];
    FILE* fp = fopen(argv[1],"r");
    int n=0;
    int sz = sizeof(buff);
    bzero(buff, sizeof(buff));
    while(  fgets(buff,sz,fp) ){
            if(buff[strlen(buff) - 1 ] == '\n')
                    buff[strlen(buff) - 1 ] = 0;
            strcpy(domain_names[n++], buff);
            bzero(buff, sizeof(buff));
    }
    fclose(fp);
    int sockfds[n];
    int next_ttl[n];
    struct sockaddr remote_endpoints[n];
    for(int i=0;i<n;i++)
            next_ttl[i] = 1;
    for(int i=0;i<MAX_TTL*n;i++)
            path_ips[i] = 0;
    fd_set rset,wset;
    FD_ZERO(&rset);
    FD_ZERO(&wset);
    int mx_sock_w=0, mx_sock_r=0;;
    for(int j=0;j<n;j++){
            struct addrinfo* res;
            res = Getaddrinfo(domain_names[j]);
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0)
                    perror("setsockopt(SO_REUSEADDR) failed");
            sockfds[j] = sockfd;

            printaddrinfo(res);
            remote_endpoints[j] = *(res->ai_addr);
            if(connect(sockfd, res->ai_addr, sizeof(struct sockaddr_in)))
                    perror("connect");
            struct sockaddr conn_sa;
            freeaddrinfo(res);
            FD_SET(sockfd, &wset);
            mx_sock_w = max(mx_sock_w , sockfds[j]);
    }
    int rawfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if(rawfd<=0){
            perror("Raw socket");
            return 0;
    }
    mx_sock_r = rawfd;
    FD_SET(rawfd, &rset);
    struct timeval tout;
    tout.tv_sec = 2;
    tout.tv_usec = 0;
    while(1){
            int ret = select(max(mx_sock_w , mx_sock_r) + 1 , &rset, &wset,NULL, &tout);
            if(ret <= 0 )// timeout
                    break;
            for(int i =0;i<n;i++){
                    if(next_ttl[i] > MAX_TTL){
                            FD_CLR(sockfds[i], &wset);
                            continue;
                    }
                    if(!FD_ISSET(sockfds[i], &wset))
                            continue;
                    struct sockaddr conn_sa;
                    u_short sport;
                    sport =  START_PORT + i*MAX_TTL + next_ttl[i]-1;
                    conn_sa =(SA) remote_endpoints[i];
                    (((struct sockaddr_in*)(&conn_sa))->sin_port = htons(sport));
                    int ttl = next_ttl[i];
                    if (setsockopt(sockfds[i],SOL_SOCKET,SO_REUSEPORT,&(int){1},sizeof(int)) < 0)
                            perror("setsockopt(SO_REUSEADDR) failed");
                  if(connect(sockfds[i],&conn_sa,sizeof(struct sockaddr_in))<0)
                            perror("connect");
                    if(setsockopt(sockfds[i], IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl))<0)
                            perror("setsockopt");
                    int sent;
                    next_ttl[i]++;
                    if ((sent = write(sockfds[i], "HELLO!", 5))<0)
                            perror("Write");
            }
            if(!FD_ISSET(rawfd, &rset)){
                    int n_read;
                    char recvmsg[1000];
                    bzero(recvmsg, 1000);
                    n_read = recvfrom(rawfd, recvmsg, 1000,0,NULL,NULL);
                    if(n_read<=0){
                            perror("Read: raw socket");
                            continue;
                    }
                    proc_icmp(recvmsg, n_read );
            }
    }
    int ip_matrix[n][MAX_TTL];
    for(int i=0;i<n*MAX_TTL;i++)
            ip_matrix[i/MAX_TTL][i%MAX_TTL] = path_ips[i];
    

    for(int i=0;i<n;i++){
            printf("%s: ", domain_names[i]);
            for(int j=0;j<MAX_TTL;j++){
                    if(!ip_matrix[i][j])
                            continue;
                   struct in_addr buf;
                   buf.s_addr = ip_matrix[i][j];
                   printf("%d - %s, ", j+1, inet_ntoa(buf));
            }
           printf("\n");
    } 
    int matching_path = 0;
    printf("the common path shared by all is: ");
    for(int j = 0;j<MAX_TTL;j++){
            int cur = ip_matrix[0][j];
            int i;
            for( i =0; i<n;i++){
                    if(cur != ip_matrix[i][j])
                            break;
                    matching_path++;
            }
            if(i!=n)
                    break;
           struct in_addr buf;
           buf.s_addr = cur;
           printf("%s, ",  inet_ntoa(buf));
    }
    printf("\n");


    return 0; 
}



