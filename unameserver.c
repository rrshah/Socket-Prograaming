#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include<pthread.h> 

struct udp_request
{
	int sockfd;
	char clreq[BUFSIZ + 1];
	struct sockaddr_in client_addr;
	socklen_t client_len;
};

int process_uname_cmd(char cmd[], char response[])
{
	FILE *fp;
	char buffer[256];
	char request[256];

	sprintf(request, "uname -%s", cmd);

	fp = popen(request, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "Failed to run command: %s", strerror(errno) );
		return -1;
	}

	while(fgets(buffer, sizeof(buffer)-1, fp) != NULL)
		strcat(response, buffer);

	pclose(fp);
	return 0;
}

void tcp_connection_handler(void *sockfd)
{
	char buffer[BUFSIZ+1], response[BUFSIZ+1] = {0};
	struct timeval tv;
	fd_set rfds;
	int ret, retval, len;
	int ssockfd = (*(int *)sockfd);

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	FD_ZERO(&rfds);
	FD_SET(ssockfd, &rfds);

	memset(response, 0, sizeof(response));
sel: ret = select(ssockfd+1, &rfds, NULL, NULL, &tv);
	if(ret == -1)
	{
		if(errno == EINTR)
			goto sel;
		fprintf(stderr, "Error: %s", strerror(errno));
	}
	else if(ret)
	{
		if(recv(ssockfd, buffer, BUFSIZ, 0) < 0)
		{
			fprintf(stderr, "Error: %s", strerror(errno));
			close(ssockfd);
		}
		
		retval = process_uname_cmd(buffer, response);
		if(retval != 0)
		{
			close(ssockfd);
			pthread_exit((void *)0);
		}
		
		if(send(ssockfd, response, strlen(response), 0) < 0)
		{
			fprintf(stderr, "Error: %s", strerror(errno));
		}
		
	}
	else
		fprintf(stderr, "No response received from server, closing connection.");

	close(ssockfd);
	pthread_exit(0);
}

void tcp_server(int port)
{
	int sockfd, ssockfd, ret, retval, len;
    struct sockaddr_in address, client_addr, my_address;
	socklen_t client_len;
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;

	if(port)
		address.sin_port = htons(port);
	else
		address.sin_port = htons(0);

	
	if(bind(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) 
    {
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if(listen(sockfd, 5) != 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if(port == 0)
	{
		len = sizeof(my_address);
		getsockname(sockfd, (struct sockaddr *) &my_address, &len);
		printf("Listening on port %d\n", ntohs(my_address.sin_port));
	}

	while(1)
	{
		pthread_t tid;
		ssockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
		if(ssockfd < 0)
		{
			if(errno == EINTR)
				continue;
			fprintf(stderr, "Error: %s", strerror(errno));
			continue;
		}


		if(pthread_create(&tid, NULL, (void *)tcp_connection_handler , (void*)&ssockfd) < 0)
        {
            fprintf(stderr, "Could not create thread : %s", strerror(errno));
            continue;
        }
					
	}

	close(sockfd);		 
}

void udp_connection_handler(void *req)
{
	char response[BUFSIZ+1];
	int retval;
	struct udp_request *request = (struct udp_request *)req;

	retval = process_uname_cmd(request->clreq, response);
	if(retval != 0)
		pthread_exit(0);
	

	if(strlen(response))
	{
		if(sendto(request->sockfd, response, strlen(response), 0, (struct sockaddr *)&request->client_addr, request->client_len) < 0)
			fprintf(stderr, "Error: %s", strerror(errno));
	}
	free(request);
	pthread_exit(0);
}

void udp_server(int port)
{
	int sockfd,  ret, retval, len;
    struct sockaddr_in address, client_addr, my_address;

	struct timeval tv;
	pid_t pid;
	
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd == -1)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
	if(port)
		address.sin_port = htons(port);


	if(bind(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) 
    {
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if(port == 0)
	{
		len = sizeof(my_address);
		getsockname(sockfd, (struct sockaddr *) &my_address, &len);
		printf("Listening on port %d\n", ntohs(my_address.sin_port));
	}

	while(1)
	{
		struct udp_request *request = (struct udp_request *)calloc(1, sizeof(struct udp_request));
		pthread_t tid;
		
		request->sockfd = sockfd;

		request->client_len = sizeof(struct sockaddr_in);
		if(recvfrom(sockfd, request->clreq, BUFSIZ, 0, (struct sockaddr *)&request->client_addr, &request->client_len) < 0)
		{
			fprintf(stderr, "Error: %s", strerror(errno));
			continue;
		}

		if(pthread_create(&tid, NULL, (void *)udp_connection_handler, (void*)request) < 0)
        {
            fprintf(stderr, "Could not create thread : %s", strerror(errno));
            continue;
        }
	}

	close(sockfd);
}


int main(int argc, char **argv)
{
	char *protocol, *port_str;
	int port = 0;
	
	if(argc < 2)
	{
		fprintf(stderr, "USAGE: unameserver -tcp/-udp [-port portNumber]\n");
		exit(EXIT_FAILURE);
	}
	
	protocol = strdup(argv[1]);
	if(argc == 4)
	{
		if(strcmp("-port", argv[2]) == 0)
		{
			port_str = argv[3];
			port = atoi(port_str);
		}
	}

	if(strncmp("-tcp", protocol, 4) == 0)
	{
		tcp_server(port);
	}
	else if(strncmp("-udp", protocol, 4) == 0)
	{
		udp_server(port);
	}

	return 0;
}

