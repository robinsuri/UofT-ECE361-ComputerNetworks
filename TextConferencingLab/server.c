#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "packet.h"
#include "user.h"
#include "session.h"

#define ULISTFILE "userlist.txt"    // File of username and passwords   
#define INBUF_SIZE 64   			// Input buffer
#define BACKLOG 10	    			// how many pending connections queue will hold

User *userList = NULL; 			// List of all users and passwords
User *userLoggedin = NULL;		// List of users logged in
Session *sessionList;			// List of all sessions created
char inBuf[INBUF_SIZE] = {0};  	// Input buffer
char port[6] = {0}; 			// Store port in string for getaddrinfo


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


// Subroutine to handle new connections
void *new_client(void *arg) {

	User *newUsr = (User*)arg;
	Session *sessJoined = NULL;	// List of sessions joined
	char buffer[BUF_SIZE];	// Buffer for recv()
	char source[MAX_NAME];	// Username (valid after logged in)
	int numbytes;
	
	// FSM states
	bool loggedin = 0;
	int session_cnt = 0;

	// The main recv() loop
	while(1) {
		memset(buffer, 0, sizeof(char) * BUF_SIZE);
		if((numbytes = recv(newUsr -> sockfd, buffer, BUF_SIZE - 1, 0)) == -1) {
			perror("recv\n");
			exit(1);
		}
		buffer[numbytes] = '\0';
		// printf("Client: received '%s'\n", buffer);
		Packet pktRecv;		// Convert data into a new packet
		Packet pktSend;		// Create packet to send
		bool toSend = 0;	// Whether to send pktSend after this loop
		stringToPacket(buffer, &pktRecv);


		/************ Finite State Machine *************/
		// Can exit anytime
		if(pktRecv.type == EXIT) {
			// Thread exits, no ACK packet sent
			// TODO: remove user from all lists
			return NULL;
		}
		
		// Before login, the user can only login or exit
		if(loggedin == 0) {	

			if(pktRecv.type == LOGIN) {	
				// Check username and password
				bool vldusr = is_valid_user(userList, newUsr);
				
				if(vldusr) {
					pktSend.type = LO_ACK;
					toSend = 1;
					loggedin = 1;
				} else {
					pktSend.type = LO_NAK;
					toSend = 1;
					// Respond with reason for failure
					if(in_list(userList, newUsr)) {	
						strcpy((char *)(pktSend.data), "Wrong password");
					} else {
						strcpy((char *)(pktSend.data), "User not registered");
					}
				}
			} else {
				pktSend.type = LO_NAK;
				toSend = 1;
				strcpy((char *)(pktSend.data), "Please log in first");
			}
		}

		// User logged in, but have not joined session
		else if(0) {

		}



		if(toSend) {
			// TODO: Add source and size for pktSend
		}
	}

}


int main() {
    // Get user input
    memset(inBuf, 0, INBUF_SIZE * sizeof(char));
	fgets(inBuf, INBUF_SIZE, stdin);
    char *pch;
    if((pch = strstr(inBuf, "server ")) != NULL) {
		memcpy(port, pch + 7, sizeof(char) * (strlen(pch + 7) - 1));
    } else {
        perror("Usage: server -<TCP port number to listen on>\n");
        exit(1);
    }
    printf("Server: starting at port %s...\n", port);


    // Load userlist at startup
    FILE *fp;
    if((fp = fopen(ULISTFILE, "r")) == NULL) {
        fprintf(stderr, "Can't open input file %s\n", ULISTFILE);
    }
    userList = init_userlist(fp);
	fclose(fp);
	printf("Server: Userdata loaded...\n");


    // Setup server
	int sockfd;     // listen on sock_fd, new connection on new_fd
	// int new_fd;		
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes = 1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;    // Use TCP
	hints.ai_flags = AI_PASSIVE;        // use my IP
	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("Server: socket");
			continue;
		}
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("Server: bind");
			continue;
		}
		break;
	}
	freeaddrinfo(servinfo); // all done with this structure
	if (p == NULL)  {
		fprintf(stderr, "Server: failed to bind\n");
		exit(1);
    }
    

    // Listen on incoming port
    if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}
	printf("Server: waiting for connections...\n");

    
	// main accept() loop
	while(1) {

		// Accept new incoming connections 
		User *newUsr = calloc(sizeof(User), 1);
		sin_size = sizeof(their_addr);
		newUsr -> sockfd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (newUsr -> sockfd == -1) {
			perror("accept");
			continue;
		}
		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s));
		pri8ntf("server: got connection from %s\n", s);

		// Update user info, and add user in connected list
		// TODO?: add IP, port to newUsr

		// Create new thread to handle the new socket
		pthread_create(&(newUsr -> p), NULL, new_client, (void *)newUsr);

	}











	// Free memory
	destroy_userlist(userList);
	destroy_userlist(userLoggedin);
	destroy_session_list(sessionList);
    return 0;
}









