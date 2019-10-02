#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>

#define BUFSIZE 1500

struct sockaddr_in target_addr;
char *buffer;
int sock;
pid_t pid;
char sendbuf[BUFSIZE];
unsigned int size = 64;
unsigned short sent = 1;
unsigned short recvd = 0;
char *target_ip = NULL;
int run = 1;

unsigned short in_cksum(unsigned short *ptr, int nbytes)
{
    register long sum;
    u_short oddbyte;
    register u_short answer;
	
    sum = 0;
    while (nbytes > 1)
	{
        sum += *ptr++;
        nbytes -= 2;
    }

    if (nbytes == 1)
	{
        oddbyte = 0;
        *((u_char *) & oddbyte) = *(u_char *) ptr;
        sum += oddbyte;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;

    return (answer);
}

void exit_handler(int signum)
{
	run = 0;
	printf("\n===========Sent %d echo requests and received %d echo responses=============\n", sent, recvd);
}

void send_ping_request(int signum)
{
	struct icmphdr *icmp;
	int sbytes;
	
	memset(sendbuf, 0, BUFSIZE);
	memcpy(sendbuf + sizeof(struct icmphdr), buffer, size);
	
	icmp = (struct icmphdr *)sendbuf;
	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	icmp->un.echo.id = pid;
	icmp->un.echo.sequence = sent++;
	icmp->checksum = in_cksum((unsigned short *)icmp, sizeof(struct icmphdr) + size);

	sbytes = sendto(sock, sendbuf, sizeof(struct icmphdr) + size, 0, (struct sockaddr*)&target_addr, sizeof(target_addr));
	if(sbytes < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
	}
	alarm(1);
}

void create_buffer(char *payload_string)
{
	int i = 0;
	int str_len = strlen(payload_string);
	int loop = size / str_len;
	
	buffer = (char *)calloc(size + 1, sizeof(char));
	if(buffer == NULL)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	for(i = 0; i < loop; i++)
	{
		memcpy(buffer + (i * str_len), payload_string, str_len);
	}

	memcpy(buffer + (loop * str_len), payload_string, size - (loop * str_len));

	buffer[size] = '\0';

}

void create_socket(char *ip)
{
	sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if(sock < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	target_addr.sin_family = AF_INET;
	inet_aton(ip, &target_addr.sin_addr);
    memset(&target_addr.sin_zero, 0, sizeof(target_addr.sin_zero));
}

void read_ping_response()
{
	struct msghdr msg;
	struct iovec io[3];
	struct iphdr ip;
	struct icmphdr icmp;
	char recvbuf[BUFSIZ];
	int recv_msg, ret;
	struct timeval timeout;
	fd_set rfds;
	FD_ZERO(&rfds);
	
	io[0].iov_base = (struct iphdr *)&ip;
	io[0].iov_len = sizeof(struct iphdr);
	io[1].iov_base = (struct icmphdr *) &icmp;
	io[1].iov_len = sizeof(struct icmphdr);
	io[2].iov_base = (char *)recvbuf;
	io[2].iov_len = BUFSIZ;


	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	msg.msg_iov = io;
	msg.msg_iovlen = 3;
	
	printf("\nPING %s (%u) bytes of data.\n", target_ip, size);
	while(run)
	{
		FD_SET(sock, &rfds);
		ret = select(sock+1, &rfds, NULL, NULL, &timeout);
		if(ret == -1)
		{
			if(errno == EINTR)
				continue;
			fprintf(stderr, "Error: %s", strerror(errno));
			continue;
		}
		else if(ret)
		{
			recv_msg = recvmsg(sock, &msg, 0);
			if(recv_msg < 0)
			{
				fprintf(stderr, "Error: %s", strerror(errno));
				continue;
			}
			if(ip.protocol != IPPROTO_ICMP)
				continue;
			
			if(icmp.type != ICMP_ECHOREPLY)
				continue;
			
			recvd++;
			printf("\n%d bytes from %s: icmp_type=%u icmp_code=%u icmp_checksum=%u icmp_identifier=%u icmp_seq=%u, data=%s",
				   size, target_ip, icmp.type, icmp.code, icmp.checksum, icmp.un.echo.id, icmp.un.echo.sequence, recvbuf);
		}
		else
		{
			fprintf(stderr, "No response received, sent %d packets and received %d packets. Closing connection.", sent, recvd);
			return;
		}
		timeout.tv_sec = 10;
		timeout.tv_usec = 0;

	}
}

int main(int argc, char **argv)
{
	int i = 1;
	char *str;
	char *payload_string = NULL;
	
	
	if(argc < 7)
	{
		fprintf(stderr, "\nUSAGE: ping -ip X.X.X.X -sz XX -string \"string to repeat\"");
		exit(EXIT_FAILURE);
	}

	while(i < argc)
	{
		str = argv[i];
		if(strcmp(str, "-ip") == 0)
		{
			target_ip = argv[++i];
		}
		else if(strcmp(str, "-sz") == 0)
		{
			size = atoi(argv[++i]);
		}
		else if(strcmp(str, "-string") == 0)
		{
			payload_string = argv[++i];
		}
		i++;
	}

	if(!target_ip || !payload_string)
	{
		fprintf(stderr, "\nUSAGE: ping -ip X.X.X.X -sz XX -string \"string to repeat\"");
		exit(EXIT_FAILURE);
	}

	create_socket(target_ip);
	create_buffer(payload_string);
	pid = getpid() & 0xffff;

	signal(SIGINT, exit_handler);
	signal(SIGALRM, send_ping_request);
	alarm(1);


	read_ping_response();
	close(sock);	
	free(buffer);
	return 0;
}
