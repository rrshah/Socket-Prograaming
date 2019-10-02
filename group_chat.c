/**********************************************
 * This is a Multicast group chat application *
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#define INIT 0
#define TALK 1
#define EXIT 2

#define INVALID 0
#define TEXT 1
#define BYE  2

#define QSIZE 512

typedef struct qnode_t
{
	char buffer[BUFSIZ*4];
	char name[BUFSIZ+1];
}qnode_t;

typedef struct queue 
{
	qnode_t qnodes[QSIZE];
    int front;
	int rear;
	int size;
	pthread_mutex_t mutex;
}queue; 


#define PACK_ADD32(p,x) { uint32_t y = htonl(x); memcpy(p,&y,sizeof(uint32_t)); p+= sizeof(uint32_t); }
#define PACK_ADD16(p,x) { uint16_t y = htons(x); memcpy(p,&y,sizeof(uint16_t)); p+= sizeof(uint16_t); }
#define PACK_ADD8(p,x) { *p = ((uint8_t)x); p+= sizeof(uint8_t); }
#define PACK_ADDn(p,src,n) { if(src != NULL) memcpy(p,src,n); else memset(p,0,n); p += n; }

#define PACK_GET32(p,sp) { *(sp) = (uint32_t) ntohl(*(uint32_t *)p); p+= sizeof(uint32_t); }
#define PACK_GET16(p,sp) { *(sp) = (uint16_t) ntohs(*(uint16_t *)p); p+= sizeof(uint16_t); }
#define PACK_GET8(p,sp) { *(sp) = (uint8_t) *(uint8_t *)p; p+= sizeof(uint8_t); }
#define PACK_GETn(p,sp,n) { if(p != NULL) {  memcpy(sp,p,n); p+=n; } else { memset(sp,0,n); } }


char name[BUFSIZ+1];
int name_len;
int state = INIT;
struct sockaddr_in mcast_group;
queue q;

void exit_handler(int signum)
{
	state = EXIT;
}

void init_queue()
{
	q.front = 0;
	q.rear = -1;
	q.size = 0;
	pthread_mutex_init(&q.mutex, NULL);
}
  
int isFull()
{
	return (q.size == QSIZE);
}
  
int isEmpty() 
{
	return (q.size == 0);
} 
  
void enqueue(qnode_t node) 
{ 
    if (isFull())
	{
		fprintf(stderr, "\nQueue is full");
        return;
	}
    q.rear = (q.rear + 1) % QSIZE; 
    strcpy(q.qnodes[q.rear].buffer, node.buffer);
	strcpy(q.qnodes[q.rear].name, node.name);
    q.size = q.size + 1;
} 
  
int dequeue(qnode_t *node) 
{
    if(isEmpty()) 
        return -1;

    strcpy(node->buffer, q.qnodes[q.front].buffer);
	strcpy(node->name, q.qnodes[q.front].name);

	q.front = (q.front + 1) % QSIZE; 
	q.size = q.size - 1; 
	return 1;
}

void print_from_queue()
{
	qnode_t node;

	pthread_mutex_lock(&q.mutex);
	while(!isEmpty())
	{
		if(dequeue(&node) == 1)
		{
			printf("\n%s > %s",node.name, node.buffer);
		}
	}
	pthread_mutex_unlock(&q.mutex);
}

qnode_t parse_buffer(char *buffer, int len, int *operation)
{
	uint8_t opcode, nlen;
	int len_seen = 0;
	uint16_t tlen;
	char sender_name[BUFSIZ];
	char *buf = NULL;
	qnode_t node;

	node.buffer[0] = 0;
	if(len < 2)
	{
		*operation = INVALID;
		return node;
	}

	PACK_GET8(buffer, &opcode);
	len -= 1;

	if(len >= 1)
	{
		if(opcode == TEXT)
		{
			PACK_GET8(buffer, &nlen);
			len -= 1;

			if(len >= (nlen + 2))
			{
				PACK_GETn(buffer, sender_name, nlen);
				sender_name[nlen] = '\0';
				strcpy(node.name, sender_name);
				len -= nlen;

				PACK_GET16(buffer, &tlen);
				len -= 2;

				if(len >= tlen)
				{
					buf = (char *)calloc(sizeof(char), tlen+1);
					if(buf == NULL)
					{
						fprintf(stderr, "Memory allocation failure: %s", strerror(errno));
						return node;
					}
					PACK_GETn(buffer, buf, tlen);
					buf[tlen] = '\0';
				}
				else
				{
					fprintf(stderr, "\nAdvertised length %d while actual %d bytes present", tlen, len);
					*operation = INVALID;
					return node;
				}

				*operation = TEXT;
			}
			else
			{
				fprintf(stderr, "\nInsufficient packet length/content");
				*operation = INVALID;
				return node;
			}
		}
		else if(opcode == BYE)
		{
			*operation = BYE;
			nlen = len;
			PACK_GETn(buffer, sender_name, nlen);
			sender_name[nlen] = '\0';
			strcpy(node.name, sender_name);
		}
		else
		{
			fprintf(stderr, "\nInvalid Opcode message sent!");
			*operation = INVALID;
			return node;
		}
	}
	else
	{
		fprintf(stderr, "\nInsufficient packet length");
		*operation = INVALID;		
	}

	if(*operation == TEXT && buf)
	{
		strcpy(node.buffer, buf);
		free(buf);
	}
	return node;
}

void receive_handler(void *fd)
{
	qnode_t node;
	int opcode, bytes, len;
	int sockfd = *(int *)fd;
	int mcast_group_len = sizeof(mcast_group);
	char buffer[BUFSIZ*4 + 1 + 256];
	len = sizeof(buffer);
	
	while(state == TALK)
	{
		bytes = recvfrom(sockfd, buffer, len, MSG_DONTWAIT, (struct sockaddr *)&mcast_group, &mcast_group_len);
		if(bytes < 0)
		{
			if(state == EXIT)
				break;
			if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			fprintf(stderr, "Error: %s", strerror(errno));
			continue;
		}

		node = parse_buffer(buffer, bytes, &opcode);

		if(opcode == TEXT)
		{
			pthread_mutex_lock(&q.mutex);
			enqueue(node);
			pthread_mutex_unlock(&q.mutex);
		}
		
		if(opcode == BYE)
		{
			printf("\n%s left the chat", node.name);
		}
	}

	pthread_exit((void *)0);
}

void send_packet(int sockfd, char *buffer, size_t buf_size, uint8_t opcode)
{
	int bytes, len;

	if(opcode == TEXT)
		len = 1 + 1 + name_len + 2 + buf_size;
	else if(opcode == BYE)
		len = 1 + name_len;

	char snd_buf[len];
	char *buf_ptr = snd_buf;
	
	PACK_ADD8(buf_ptr, opcode);

	if(opcode == TEXT)
	{
		PACK_ADD8(buf_ptr, name_len);
	}
	
	PACK_ADDn(buf_ptr, name, name_len);

	if(opcode == TEXT)
	{
		PACK_ADD16(buf_ptr, buf_size);
		PACK_ADDn(buf_ptr, buffer, buf_size);
	}

	bytes = sendto(sockfd, snd_buf, len, 0, (struct sockaddr *)&mcast_group, sizeof(mcast_group));
	if(bytes < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
	}
}

void communication_handler(int sockfd)
{
	pthread_t tid;
	char buf[BUFSIZ*4];
	
	if(pthread_create(&tid, NULL, (void *)receive_handler, (void*)&sockfd) < 0)
	{
		fprintf(stderr, "Could not create thread : %s", strerror(errno));
		return;
	}

	scanf("%c", buf);
	while(state == TALK)
	{
		printf("\nPlease enter the text: ");
		fgets(buf, sizeof(buf), stdin);

		if(state == EXIT)
		{
			print_from_queue();
			break;
		}

		send_packet(sockfd, buf, strlen(buf) - 1, TEXT);
		print_from_queue();
	}

	if(state == EXIT)
		send_packet(sockfd, NULL, 0, BYE);
	
	pthread_join(tid, NULL);
}

int connect_to_group(char *multicast_ip, int multicast_port)
{
    int sock, reuse = 1, loopback = 0;
	struct ip_mreq mreq;

	memset(&mcast_group, 0, sizeof(mcast_group));
	mcast_group.sin_family = AF_INET;
	mcast_group.sin_port = htons(multicast_port);
	inet_aton(multicast_ip, &mcast_group.sin_addr);
     
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
	}

	if(setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
	}

	if(bind(sock, (struct sockaddr*)&mcast_group, sizeof(mcast_group)) < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}


	/* Preparatios for using Multicast */ 
	mreq.imr_multiaddr = mcast_group.sin_addr;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	/* Tell the kernel we want to join that multicast group. */
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
	{
		fprintf(stderr, "Error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	state = TALK;
	return sock;
}

int main(int argc, char *argv[])
{
    char *multicast_ip;
    unsigned short multicast_port;
	int i = 1;
	char *str;
	int sockfd;
	
	if(argc < 5)
	{
		fprintf(stderr, "\nUSAGE: multicast_chat -mcip X.X.X.X -port XX");
		exit(EXIT_FAILURE);
	}

	while(i < argc)
	{
		str = argv[i];
		if(strcmp(str, "-mcip") == 0)
		{
			multicast_ip = argv[++i];
		}
		else if(strcmp(str, "-port") == 0)
		{
			multicast_port = atoi(argv[++i]);
		}
		i++;
	}

	signal(SIGINT, exit_handler);
	printf("\nEnter your name :: ");
	scanf("%s", name);
	name_len = strlen(name);
	
	sockfd = connect_to_group(multicast_ip, multicast_port);
	init_queue();
	communication_handler(sockfd);

	printf("\n===============Closing chat session==================\n");
	close(sockfd);
	return 0;
}
