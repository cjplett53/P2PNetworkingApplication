/* Index Server 

Message types:
R - used for registration
A - used by the server to acknowledge the success of registration
Q - used by chat users for de-registration
D - download content between peers
C - Content of download
S - Search content
L - Local content list
E - Error messages from the server
U - Usage update (not part of system requirements, added as a workaround for updating usage counts of servers)

*/

#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>

#define QUIT "quit"
#define SERVER_PORT 10000   
#define BUFLEN  100     
#define NAMESIZE 20
#define MAXCON  200

// Regular pdu
typedef struct 
{
char    type;
char    data[BUFLEN];
} PDU;  
PDU pdu;

// PDU used to send data back and forth between peer and index server
typedef struct {
    char type;
    char user[NAMESIZE];
    char content[NAMESIZE];
    struct sockaddr_in address;
}RPDU;
RPDU rpdu_array[MAXCON]; // local list stored here

struct {
    int     val;
    char    name[NAMESIZE];
} table[MAXCON];  //Keep Track of the registered content

char usr[NAMESIZE];
int     s_sock;
int     fd, nfds;
fd_set  rfds, afds;
int     portNum;
int     numIP;
char	hostIP[NAMESIZE];
struct  sockaddr_in *global_server;

bool	select_username(int, struct sockaddr_in *, char *); 
void    registration(int, char *, struct sockaddr_in *, char *); 
void    search_content(int, char *, struct sockaddr_in *, RPDU *);
void    client_download(int, RPDU *, struct sockaddr_in *);
void    server_download(int);
void    deregistration(int, char *, struct sockaddr_in *, char *);
void    online_list(int, struct sockaddr_in *);
void    local_list();
void    quit(int, struct sockaddr_in *);
void    handler(int);
int     contentCount=0;
int 	i;

int     tcp_listen_socks[MAXCON];
int     num_listen_socks = 0;
int     tcp_conn_socks[MAXCON];
int     num_conn_socks = 0;

int main(int argc, char **argv)
{
    int s_port = SERVER_PORT;
    int n;
    int alen = sizeof(struct sockaddr_in);
    struct  hostent     *hp;
    struct  sockaddr_in server;
    char    c, *host, name[NAMESIZE];
    struct  sigaction sa;

    switch(argc){
        case 2:
            host = argv[1];
            break;
        case 3:
            host = argv[1];
            s_port = atoi(argv[2]);
            break;
        default:
            printf("Usage: %s host [port]\n", argv[0]);
            exit(1);
    }

    // UDP Connection with the index server 
    memset(&server, 0, alen);
    server.sin_family = AF_INET;
    server.sin_port = htons(s_port);
    if((hp = gethostbyname(host)))
        memcpy(&server.sin_addr, hp->h_addr, hp->h_length);
    else 
        if ((server.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE ){
            printf("Can't get host entry \n");
            exit(1);
        }
    s_sock = socket(PF_INET, SOCK_DGRAM, 0); // Allocate a socket for a UDP connection with the index server
    if (s_sock < 0){
        printf("Can't create socket \n");
        exit(1);
    } 
    // We don't need to connect the UDP socket since we'll use sendto and recvfrom
	
	bool nametaken=false;
    printf("Choose a user name\n");
    scanf("%s",usr);
	nametaken=select_username(s_sock, &server, usr);
    while(nametaken){
		printf("User name taken, select different name\n");
		scanf("%s", usr);
		nametaken = select_username(s_sock, &server, usr);
	}

    printf("Choose a port number for peer-to-peer TCP connections (i.e. 3000)\n");
    scanf("%d",&portNum);

    printf("Enter IP address of host machine (i.e. 10.1.1.n)\n");
    scanf("%s", hostIP);

    /* Initialization of SELECT structure and table structure  */
    FD_ZERO(&afds);             // Clear all file descriptors in active file descriptor set (afds)
    FD_SET(s_sock, &afds);      // Listen to the index server
    FD_SET(0, &afds);           // Listen to the user at the terminal
    nfds = s_sock + 1;
    if (0 >= nfds)
        nfds = 1;

    for(n=0; n<MAXCON; n++)
        table[n].val = -1;

    /*  Setup signal handler      */
	global_server = &server;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    /* Main Loop    */
    while(1){

        printf("\nCommand:\n");
        
        memcpy(&rfds, &afds, sizeof(rfds));
        if (select(nfds, &rfds, NULL, NULL, NULL) == -1){
            printf("select error: %s\n",strerror(errno));
            exit(1);
        }

        for (fd = 0; fd < nfds; fd++) {
            if (FD_ISSET(fd, &rfds)) {
                if (fd == 0) {  /* Command from the user  */
                    c = getchar();

                    /*  Command options */
                    if(c=='?'){
                        printf("\nR-Content Registration\nD-Content Download Request\nS-Search for Content and the Associated Content Server\n");
                        printf("T-Content Deregistration\nO-List of Online Registered Content\n");
                        printf("Q-Quit\n\n");
                        continue;
                    }
                    /*  Content registration */
                    if(c=='R'){ 
                        printf("R-Content Registration\n");
                        char file_name[NAMESIZE];
                        printf("Enter file name:\n");
                        scanf("%s", file_name);
                        registration(s_sock, usr, &server, file_name);
                        // After registration, we might have added a larger new listening socket
                        // Update nfds if necessary
                        for (i = 0; i < num_listen_socks; i++) {
                            if (tcp_listen_socks[i] >= nfds)
                                nfds = tcp_listen_socks[i] + 1;
                        }
                    }

                    /*  Search for on-line content   */
                    if(c == 'S'){
                        printf("S-Search for Content and the Associated Content Server\n");
                        RPDU rpdu;
                        search_content(s_sock, usr, &server, &rpdu);
                    }

                    /*  List on-line content    */
                    if(c == 'O'){
                        printf("O-List of Online Registered Content\n");
                        online_list(s_sock, &server);
                    }

                    /*  Download content    */
                    if(c=='D'){
                        printf("D-Content Download Request\n");
                        RPDU rpdu;
                        search_content(s_sock, usr, &server, &rpdu);
                        client_download(s_sock, &rpdu, &server);
                    }

                    /*  Deregistration  */
                    if(c == 'T'){
                        printf("T-Content Deregistration\n");
			            char file_name[NAMESIZE];
    			        printf("Which content would you like to deregister?\n");
    			        scanf("%s", file_name);
                        deregistration(s_sock, usr, &server, file_name);
                    }
		    
		            /*  List local content */
		            if(c == 'L'){
			            printf("L-List of Local Registered Content\n");
  			            local_list();
		            }
			
                    /*  Quit    */
                    if(c == 'Q'){
                        quit(s_sock, &server);
                        exit(0);
                    }

                } 
                else{ 
                    // Handle data from index server if necessary
                    if (fd == s_sock){}

                    // Determine if fd is a listening socket or a connected socket
                    else{
                        bool is_listen_sock = false;
                        for (i = 0; i < num_listen_socks; i++) {
                            if (fd == tcp_listen_socks[i]) {
                                is_listen_sock = true;
                                break;
                            }
                        }
                        if (is_listen_sock) {
                            // Accept new connection
                            int newfd;
                            struct sockaddr_in client_addr;
                            socklen_t addrlen = sizeof(client_addr);
                            newfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
                            if (newfd < 0) {
                                perror("accept failed");
                                exit(1);
                            }
                            printf("Accepted connection from %s\n", inet_ntoa(client_addr.sin_addr));
                            FD_SET(newfd, &afds);
                            if (newfd >= nfds)
                                nfds = newfd + 1;
                                tcp_conn_socks[num_conn_socks++] = newfd;
                        } 
                        else{
                            // Handle data from connected socket fd
                            server_download(fd);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

void quit(int s_sock, struct sockaddr_in *server)
{
    /* De-register all the registrations in the index server  */
    printf("Quitting and de-registering %d content...\n", contentCount);
	while(contentCount>0){
		printf("Deregistering %s\n", rpdu_array[0].content);
		deregistration(s_sock, usr, server, rpdu_array[0].content);
	}
	RPDU rpdu;
	rpdu.type='Q';
	strcpy(rpdu.user, usr);
	if(sendto(s_sock, &rpdu, sizeof(rpdu), 0, (struct sockaddr *)server, sizeof(*server))<0){
		fprintf(stderr, "Error sending data\n");
		exit(1);
	}
}

void local_list(){
    /* List local content  */ 
    printf("Local registered content:\n");
    for (i = 0; i < contentCount; i++) {
        printf("\t%s\n", rpdu_array[i].content);
    }
}

void online_list(int s_sock, struct sockaddr_in *server){
    // Create O packet
    PDU opdu;
    opdu.type='O';
    strcpy(opdu.data, "List request");

    // Send pdu through udp to index server
    if(sendto(s_sock, &opdu, sizeof(opdu), 0, (struct sockaddr *)server, sizeof(*server))<0){
        fprintf(stderr, "Error sending data\n");
        exit(1);
    }
    
    int n;
    PDU pdu;
    socklen_t addr_len = sizeof(struct sockaddr_in); 
    
    printf("Contents available for download:\n");
    while((n=recvfrom(s_sock, &pdu, sizeof(pdu), 0, (struct sockaddr *)server, &addr_len))>0){
        if(n<0){     
            fprintf(stderr, "Error receiving data\n");
            exit(1);
        }
        if(pdu.type=='Q')
            break;
        printf("\tContent: %s\n", pdu.data);
    }
}

/***************************************************************************
/*                         PEER SERVER DOWNLOAD                           **
/**************************************************************************/

void server_download(int fd){
    
    PDU dpdu, cpdu;
    int n;
    int total_read = 0;
    char *p = (char *)&dpdu;

    // Read D-type PDU from client
    while (total_read < sizeof(dpdu)) {
        n = read(fd, p + total_read, sizeof(dpdu) - total_read);
        if (n <= 0) {
            close(fd);
            FD_CLR(fd, &afds);
            return;
        }
        total_read += n;
    }

    if (dpdu.type != 'D') {
        printf("Received unexpected PDU type: %c\n", dpdu.type);
        close(fd);
        FD_CLR(fd, &afds);
        return;
    }

    // Check if content is available
    bool content_found = false;
    for (i = 0; i < contentCount; i++) {
        if (strcmp(rpdu_array[i].content, dpdu.data) == 0) {
            content_found = true;
            break;
        }
    }

    if (!content_found) {
        printf("Requested content not found\n");
        close(fd);
        FD_CLR(fd, &afds);
        return;
    }

    // Open the file and send its contents
    FILE *fp = fopen(dpdu.data, "rb");
    if (fp == NULL) {
        perror("Error opening file");
	cpdu.type='E';
	if (write(fd, &cpdu.type, sizeof(cpdu.type)) < 0) {
            perror("Error writing to socket");
        }
        close(fd);
        FD_CLR(fd, &afds);
        return;
    }

    cpdu.type = 'C';
    while ((n = fread(cpdu.data, 1, BUFLEN, fp)) > 0) {
        // Send the PDU type and the actual data read
        if (write(fd, &cpdu.type, sizeof(cpdu.type)) < 0) {
            perror("Error writing to socket");
            break;
        }
        if (write(fd, cpdu.data, n) < 0) {
            perror("Error writing to socket");
            break;
        }
    }
    fclose(fp);
    close(fd);
    FD_CLR(fd, &afds);
    printf("Content sent and connection closed\n");
}

void search_content(int s_sock, char *name, struct sockaddr_in *server, RPDU *rpdu){
    
    char file_name[NAMESIZE];
    printf("Enter filename you wish to find\n");
    scanf("%s", file_name);

    RPDU spdu;
    spdu.type='S';
    strcpy(spdu.content, file_name);
    strcpy(spdu.user, name);
    
    // Send pdu through udp to index server
    if(sendto(s_sock, &spdu, sizeof(spdu), 0, (struct sockaddr *)server, sizeof(*server))<0){
        fprintf(stderr, "Error sending data\n");
        exit(1);
    }

    int n;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    
    // Wait for response from index server
    if ((n=recvfrom(s_sock, rpdu, sizeof(*rpdu), 0,(struct sockaddr *)server, &addr_len)) < 0){
        fprintf(stderr, "Error receiving data\n");
    }

    if (rpdu->type == 'S') {
        printf("Receiving %c, %s, %s\n", rpdu->type, rpdu->user, rpdu->content);
        printf("IP Address: %s\n", inet_ntoa(rpdu->address.sin_addr));
        printf("Port: %d\n", ntohs(rpdu->address.sin_port));
    } 
    else{
        printf("Content not found.\n");
    }
}


/***************************************************************************
/*                         PEER CLIENT DOWNLOAD                           **
/**************************************************************************/

void client_download(int s_sock, RPDU *rpdu, struct sockaddr_in *server){
    
    int sockfd;
    struct sockaddr_in server_addr;
    PDU dpdu, cpdu;
    int n;

    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        return;
    }

    // Prepare server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = rpdu->address.sin_port;  // Port of the content server
    server_addr.sin_addr = rpdu->address.sin_addr;  // IP of the content server

    // Connect to the content server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to content server");
        close(sockfd);
        return;
    }

    printf("Connected to content server at %s:%d\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    // Send D-type PDU with content name
    dpdu.type = 'D';
    strcpy(dpdu.data, rpdu->content);
    n = write(sockfd, &dpdu, sizeof(dpdu));
    if (n < 0) {
        perror("Error writing to socket");
        close(sockfd);
        return;
    }

    // Open file to save the received content
    FILE *fp = fopen(rpdu->content, "wb");
    if (fp == NULL) {
        perror("Error opening file for writing");
        close(sockfd);
        return;
    }

    // Receive C-type PDUs and write to file
    while (1) {
        // Read the PDU type
        n = read(sockfd, &cpdu.type, sizeof(cpdu.type));
        if (n <= 0) {
            break;
        }
        if (cpdu.type == 'E') {
            char command[BUFLEN];
            sprintf(command, "rm %s", rpdu->content);
            printf("Received unexpected PDU type: %c\n", cpdu.type);
		    int status = system(command);
		    return;
            
        }
        // Read the data
        n = read(sockfd, cpdu.data, BUFLEN);
        if (n <= 0) {
            break;
        }
        fwrite(cpdu.data, 1, n, fp);  // Write data to file
    }

    if (n < 0) {
        perror("Error reading from socket");
    } 
    else{
        printf("Download completed\n");
        // Register the content
        registration(s_sock, usr, server, rpdu->content);

        // Send usage update to index server
        RPDU updu;
        updu.type = 'U';
        strcpy(updu.content, rpdu->content);
        strcpy(updu.user, rpdu->user); // The user from whom we downloaded
        updu.address = rpdu->address;  // The address of the peer we downloaded from

        if(sendto(s_sock, &updu, sizeof(updu), 0, (struct sockaddr *)server, sizeof(*server))<0){
            fprintf(stderr, "Error sending usage update\n");
        } 
        else{
            printf("Usage update sent to index server\n");
        }
    }
    fclose(fp);
    close(sockfd);
}


void deregistration(int s_sock, char *name, struct sockaddr_in *server, char *file_name){
    
    int index;

    bool local=false;
    for(i=0; i<contentCount; ++i){
        if(strcmp(file_name, rpdu_array[i].content)==0){
	        local=true;
	        index=i;
	    }
    }
    if(local){
	    RPDU tpdu;
	    tpdu.type='T';
	    strcpy(tpdu.content, file_name);
	    strcpy(tpdu.user, name);
	    tpdu.address.sin_port = rpdu_array[index].address.sin_port;
	    if (inet_pton(AF_INET, hostIP, &tpdu.address.sin_addr) <= 0) {
	        perror("Invalid IP address");
	        exit(1);
	    }
        // Send pdu through udp to index server
        if(sendto(s_sock, &tpdu, sizeof(tpdu), 0, (struct sockaddr *)server, sizeof(*server))<0){
            fprintf(stderr, "Error sending data\n");
            exit(1);
        }

	    int n;
        PDU pdu;
        socklen_t addr_len = sizeof(struct sockaddr_in);
    
        // Wait for response from index server
        if ((n=recvfrom(s_sock, &pdu, sizeof(pdu), 0,(struct sockaddr *)server, &addr_len)) < 0){
            fprintf(stderr, "Error receiving data\n");
        }
        if(pdu.type=='A'){
            printf("\t%s\n", pdu.data);
	    for (i = index; i < contentCount - 1; ++i) {
    		rpdu_array[i] = rpdu_array[i + 1];
	    }
	    // Decrement the content count
	    --contentCount;
	    }
        else
            printf("\t%s\n", pdu.data);
    }
    else{
	    printf("Content not local: No deregistering possible\n");
    }
}

void registration(int s_sock,char *name, struct sockaddr_in *server, char *file_name){
    // Create a TCP socket
    int tcp = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp == -1){
        fprintf(stderr, "Can't create tcp socket\n");
        exit(1);
    }

    // Define the address and port
    struct sockaddr_in tcp_server;

    bzero((char *)&tcp_server, sizeof(struct sockaddr_in));
    tcp_server.sin_family = AF_INET;
    tcp_server.sin_port = htons(portNum);  // User-specified port number
    ++portNum;

    // Convert the input IP address to the appropriate format
    if (inet_pton(AF_INET, hostIP, &tcp_server.sin_addr) <= 0) {
        perror("Invalid IP address");
        exit(1);
    }

    if (bind(tcp, (struct sockaddr *)&tcp_server, sizeof(tcp_server)) == -1){
        perror("Can't bind name to tcp socket");
        exit(1);
    }
    listen(tcp, 5);
    
    // Print the IP address and port number for verification
    printf("Server IP Address: %s\n", hostIP);
    printf("Server Port Number: %d\n", ntohs(tcp_server.sin_port));

    // Add the TCP socket to afds set
    FD_SET(tcp, &afds);

    // Update nfds to the maximum descriptor value plus one
    if (tcp >= nfds) {
        nfds = tcp + 1;
    }
		
    // Keep track of listening sockets
    tcp_listen_socks[num_listen_socks++] = tcp;

    // Create registration packet
    RPDU rpdu;
    rpdu.type='R';
    strcpy(rpdu.content, file_name);
    strcpy(rpdu.user, usr);
    rpdu.address=tcp_server;
    rpdu_array[contentCount++]=rpdu;

    // Send pdu through udp to index server
    if(sendto(s_sock, &rpdu, sizeof(rpdu), 0, (struct sockaddr *)server, sizeof(*server))<0){
        fprintf(stderr, "Error sending data\n");
        exit(1);
    }
    
    int n;
    PDU pdu;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    
    // Wait for response from index server
    if ((n=recvfrom(s_sock, &pdu, sizeof(pdu), 0,(struct sockaddr *)server, &addr_len)) < 0){
        fprintf(stderr, "Error receiving data\n");
    }
    
    if(pdu.type=='A'){
        printf("\t%s\n", pdu.data);
    }
    else{
        printf("Error: %s\n", pdu.data);
        scanf("%s",usr);
        // Recursive call to try registering again
        registration(s_sock, usr, server, file_name);
    }
}

bool select_username(int s_sock, struct sockaddr_in *server, char *name){
	bool found = false;	
	RPDU updu;
	updu.type='N';
	strcpy(updu.user, usr);
	if(sendto(s_sock, &updu, sizeof(updu), 0, (struct sockaddr *)server, sizeof(*server))<0){
        fprintf(stderr, "Error sending data\n");
        exit(1);
    }
	int n;
    PDU pdu;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    
    // Wait for response from index server
    if ((n=recvfrom(s_sock, &pdu, sizeof(pdu), 0, (struct sockaddr *)server, &addr_len)) < 0){
        fprintf(stderr, "Error receiving data\n");
    }
	if(pdu.type=='F'){
		found = true;
	}
	else{
		found = false;
	}
	return found;
}

void handler(int sig){
    quit(s_sock, global_server);
    exit(0);
}
