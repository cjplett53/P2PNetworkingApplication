/* Index Server 

Message types:
R - used for registration
A - used by the server to acknowledge the success of registration
Q - used by chat users for de-registration
D - download content between peers (not used here)
C - Content (not used here)
S - Search content
L - Location of the content server peer
E - Error messages from the Server
U - Usage update (not part of system requirements, added as a workaround for updating usage counts of servers)

*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define MSG1 "Cannot find content"
#define BUFLEN  100
#define NAMESIZE    20
#define MAX_NUM_CON 200
#define MAXCON      200
#define MAX_CONTENT 200
#define MAX_SERVERS_PER_CONTENT 10

typedef struct {
    char    usr[NAMESIZE];
    struct sockaddr_in addr;
    short   token;      
    struct entry *next;
} ENTRY; 

typedef struct{
    char name[NAMESIZE];
    ENTRY   *head;
} LIST;
LIST    list[MAX_NUM_CON];

typedef struct{
    char type;
    char data[BUFLEN];
} PDU;

// Registration for this peer
typedef struct {
    char type;
    char user[NAMESIZE];
    char content[NAMESIZE];
    struct sockaddr_in address;
}RPDU;

// New data structures
typedef struct {
    char user[NAMESIZE];
    struct sockaddr_in address;
    int usage_count; // number of times this server has been used
} ServerInfo;

typedef struct {
    char content[NAMESIZE];
    ServerInfo servers[MAX_SERVERS_PER_CONTENT];
    int num_servers;
} ContentEntry;

ContentEntry content_list[MAX_CONTENT];
int num_contents = 0;
char usernames[50][NAMESIZE];
int num_users = 0;

void check_username(int, RPDU *, struct sockaddr_in *);
void search(int, RPDU *, struct sockaddr_in *);
void registration(int, RPDU *, struct sockaddr_in *); 
void deregistration(int, RPDU *, struct sockaddr_in *);
void usage_update(int, RPDU *, struct sockaddr_in *); // New function
void client_exit(int, RPDU *, struct sockaddr_in *);

/*
 *------------------------------------------------------------------------
 * main - Iterative UDP server for Content Indexing service
 *------------------------------------------------------------------------
 */

int
main(int argc, char *argv[])
{
    struct sockaddr_in sin, *p_addr;   /* the from address of a client */
    ENTRY   *p_entry;
    char    *service = "10000";    /* service name or port number */
    char    name[NAMESIZE], usr[NAMESIZE];
    int alen = sizeof(struct sockaddr_in);   /* from-address length     */
    int     s, n, i, len,p_sock;        /* socket descriptor and socket type    */
    int pdulen=sizeof(PDU);
    struct  hostent         *hp;
    RPDU    rpdu;
    struct sockaddr_in fsin;    /* the from address of a client */


    for(n=0; n<MAX_NUM_CON; n++)
        list[n].head = NULL;

    switch (argc) {
    case    1:
        break;
    case    2:
        service = argv[1];
        break;
    default:
        fprintf(stderr, "usage: server [port]\n");
        exit(1);
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;

   /* Map service name to port number */
    sin.sin_port = htons((u_short)atoi(service));

    /* Allocate a socket */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0){
        fprintf(stderr, "can't create socket\n");
        exit(1);
    }

    /* Bind the socket */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        fprintf(stderr, "can't bind to %s port\n", service);
        exit(1);
    }

    while (1) {
        if ( (n=recvfrom(s, &rpdu, sizeof(rpdu), 0,(struct sockaddr *)&fsin, &alen)) < 0) { // Receiving packet from client
          printf("recvfrom error: n=%d\n",n);
          continue;
        }

        /*  Content Registration Request            */
        if(rpdu.type == 'R'){
            registration(s, &rpdu, &fsin);   
        }

        /* Search Content      */
        else if(rpdu.type == 'S'){ 
            search(s, &rpdu, &fsin);
        }

        /* List current Content */
        else if(rpdu.type == 'O'){
            int i;
            PDU opdu;
            opdu.type='O';
            for(i=0; i<num_contents; ++i){
                strcpy(opdu.data, content_list[i].content);
                if(sendto(s, &opdu, sizeof(opdu), 0, (struct sockaddr *)&fsin, sizeof(fsin))<0){
                    fprintf(stderr, "Error sending data\n");
                    exit(1);
                }
            }
            opdu.type='Q';
            opdu.data[0]='\0';
            if(sendto(s, &opdu, sizeof(opdu), 0, (struct sockaddr *)&fsin, sizeof(fsin))<0){
                fprintf(stderr, "Error sending data\n");
                exit(1);
            }
        }
            
        /*  De-registration     */
        else if(rpdu.type == 'T'){
            deregistration(s, &rpdu, &fsin);
        }

        /* Usage Update */
        else if(rpdu.type == 'U'){
            usage_update(s, &rpdu, &fsin);
        }

	else if(rpdu.type == 'N'){
		check_username(s, &rpdu, &fsin);
	}

	else if(rpdu.type == 'Q'){
		client_exit(s, &rpdu, &fsin);
	}

    }
    return 0;
}

void client_exit(int s, RPDU *rpdu, struct sockaddr_in *fsin){
	int i, index;
	for(i=0; i<num_users; ++i){
		if(strcmp(rpdu->user, usernames[i]) == 0){
			index=i;
			break;
		}
	}
	for(i=index; i<num_users-1; ++i){
		strcpy(usernames[i], usernames[i+1]);
	}
	--num_users;
}

void search(int s, RPDU *rpdu, struct sockaddr_in *fsin)
{
    int i, j, index;
    bool content_found = false;
    for (i = 0; i < num_contents; ++i) {
        if (strcmp(content_list[i].content, rpdu->content) == 0) {
            content_found = true;
            // Find the server with least usage_count
            int min_usage = content_list[i].servers[0].usage_count;
            int min_index = 0;
            for (j = 1; j < content_list[i].num_servers; j++) {
                if (content_list[i].servers[j].usage_count < min_usage) {
                    min_usage = content_list[i].servers[j].usage_count;
                    min_index = j;
                }
            }
            // Prepare the response
            RPDU spdu;
            spdu.type='S';
            strcpy(spdu.user, content_list[i].servers[min_index].user);
            strcpy(spdu.content, content_list[i].content);
            spdu.address = content_list[i].servers[min_index].address;
            printf("Providing server %s for content %s\n", spdu.user, spdu.content);
            if(sendto(s, &spdu, sizeof(spdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
                fprintf(stderr, "Error sending data\n");
                exit(1);
            }
            return;
        }
    }
    // If content not found, send error
    PDU epdu;
    epdu.type='E';
    strcpy(epdu.data, "Content not found");
    if(sendto(s, &epdu, sizeof(epdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
        fprintf(stderr, "Error sending data\n");
        exit(1);
    }
}

void usage_update(int s, RPDU *rpdu, struct sockaddr_in *fsin)
{
    int i, j;
    bool content_found = false;
    bool server_found = false;

    // Find the content
    for (i = 0; i < num_contents; ++i) {
        if (strcmp(content_list[i].content, rpdu->content) == 0) {
            content_found = true;
            // Find the server
            for (j = 0; j < content_list[i].num_servers; j++) {
                if (strcmp(content_list[i].servers[j].user, rpdu->user) == 0 &&
                    content_list[i].servers[j].address.sin_addr.s_addr == rpdu->address.sin_addr.s_addr &&
                    content_list[i].servers[j].address.sin_port == rpdu->address.sin_port) {
                    // Increment usage count
                    content_list[i].servers[j].usage_count++;
                    printf("Incremented usage count for server %s, content %s, new count: %d\n",
                           content_list[i].servers[j].user, content_list[i].content,
                           content_list[i].servers[j].usage_count);
                    server_found = true;
                    break;
                }
            }
            break;
        }
    }

    if (!content_found || !server_found) {
        printf("Usage update: content or server not found\n");
    }
    // No need to send acknowledgment
}

void deregistration(int s, RPDU *rpdu, struct sockaddr_in *fsin)
{
    int i, j, k;
    bool content_found = false;
    bool server_found = false;
    
    // Find the content in content_list
    for (i = 0; i < num_contents; i++) {
        if (strcmp(content_list[i].content, rpdu->content) == 0) {
            content_found = true;
            // Find the server in the content's server list
            for (j = 0; j < content_list[i].num_servers; j++) {
                if (strcmp(content_list[i].servers[j].user, rpdu->user) == 0 &&
                    content_list[i].servers[j].address.sin_addr.s_addr == rpdu->address.sin_addr.s_addr &&
                    content_list[i].servers[j].address.sin_port == rpdu->address.sin_port) {
                    server_found = true;
                    // Remove this server from the list
                    for (k = j; k < content_list[i].num_servers - 1; k++) {
                        content_list[i].servers[k] = content_list[i].servers[k + 1];
                    }
                    content_list[i].num_servers--;
                    break;
                }
            }
            if (content_list[i].num_servers == 0) {
                // Remove the content entry
                for (k = i; k < num_contents - 1; k++) {
                    content_list[k] = content_list[k + 1];
                }
                num_contents--;
            }
            break;
        }
    }
    if (server_found) {
        printf("Deregistered %s from user %s\n", rpdu->content, rpdu->user);
        PDU apdu;
        apdu.type='A';
        strcpy(apdu.data, "Acknowledged");
        if(sendto(s, &apdu, sizeof(apdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
            fprintf(stderr, "Error sending data\n");
            exit(1);
        }
    } else {
        PDU epdu;
        epdu.type='E';
        strcpy(epdu.data, "Content or server not found");
        if(sendto(s, &epdu, sizeof(epdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
            fprintf(stderr, "Error sending data\n");
            exit(1);
        }
    }
}

void registration(int s, RPDU *rpdu, struct sockaddr_in * fsin)
{
    int i, j;
    bool content_found = false;
    bool server_exists = false;

    // Check if the content exists in content_list
    for (i = 0; i < num_contents; i++) {
        if (strcmp(content_list[i].content, rpdu->content) == 0) {
            content_found = true;
            // Check if this server is already registered for this content
            for (j = 0; j < content_list[i].num_servers; j++) {
                if (strcmp(content_list[i].servers[j].user, rpdu->user) == 0
                    && content_list[i].servers[j].address.sin_addr.s_addr == rpdu->address.sin_addr.s_addr &&
                    content_list[i].servers[j].address.sin_port == rpdu->address.sin_port) {
                    server_exists = true;
                    break;
                }
            }
            if (!server_exists) {
                if (content_list[i].num_servers < MAX_SERVERS_PER_CONTENT) {
                    // Add server to servers list
                    strcpy(content_list[i].servers[content_list[i].num_servers].user, rpdu->user);
                    content_list[i].servers[content_list[i].num_servers].address = rpdu->address;
                    content_list[i].servers[content_list[i].num_servers].usage_count = 0;
                    content_list[i].num_servers++;
                } 
				else{
                    // Maximum servers reached for this content
                    PDU epdu;
                    epdu.type='E';
                    strcpy(epdu.data, "Maximum servers reached for this content");
                    if(sendto(s, &epdu, sizeof(epdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
                        fprintf(stderr, "Error sending data\n");
                        exit(1);
                    }
                    return;
                }
            }
            break;
        }
    }

    if (!content_found) {
        if (num_contents < MAX_CONTENT) {
            // Add new content to content_list
            strcpy(content_list[num_contents].content, rpdu->content);
            content_list[num_contents].num_servers = 1;
            strcpy(content_list[num_contents].servers[0].user, rpdu->user);
            content_list[num_contents].servers[0].address = rpdu->address;
            content_list[num_contents].servers[0].usage_count = 0;
            num_contents++;
        } else {
            // Maximum content limit reached
            PDU epdu;
            epdu.type='E';
            strcpy(epdu.data, "Maximum content limit reached");
            if(sendto(s, &epdu, sizeof(epdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
                fprintf(stderr, "Error sending data\n");
                exit(1);
            }
            return;
        }
    }

    // Send acknowledgement
    printf("Registration of content: %s by user: %s\n", rpdu->content, rpdu->user);
    PDU apdu;
    apdu.type='A';
    strcpy(apdu.data, "Acknowledged");
    if(sendto(s, &apdu, sizeof(apdu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
        fprintf(stderr, "Error sending data\n");
        exit(1);
    }
}

void check_username(int s, RPDU *rpdu, struct sockaddr_in * fsin){
	bool found = false;
	int i;

	for(i=0; i<num_users; ++i){
		if(strcmp(rpdu->user, usernames[i]) == 0){
			found = true;
			break;
		}
	}

	if(found){
		PDU updu;
		updu.type='F';
		strcpy(updu.data, "Found username");
		if(sendto(s, &updu, sizeof(updu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
       		fprintf(stderr, "Error sending data\n");
       		exit(1);
    	}
	}

	else{
		strcpy(usernames[num_users++], rpdu->user);
		PDU updu;
		updu.type='N';
		strcpy(updu.data, "Username not found");
		if(sendto(s, &updu, sizeof(updu), 0, (struct sockaddr *)fsin, sizeof(*fsin))<0){
       		fprintf(stderr, "Error sending data\n");
       		exit(1);
    	}
	}
}

