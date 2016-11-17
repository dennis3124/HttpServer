#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

void zombieHandler(int sig) {
	while(waitpid(-1, NULL, WNOHANG) > 0);
}
void poolSlave (int socket);
int QueueLength = 5;
void processRequest( int socket );
pthread_mutex_t mt;
pthread_mutexattr_t mattr;
bool checkPath(char * filePath, char * toCheck);
bool checkType(char* dir, char* type);
void processThread(int socket);
int
main( int argc, char ** argv )
{
	int port;
	char* opt;
	// Print usage if not enough arguments
  	if (argc==1) {
		//no port specified, assume default port 20016
		fprintf(stderr, "No port detected, using default port of '20016'\n");
		port = 20016;
	} else if (argc ==2 ) {
		//either port specified or flag specified
		//check

		if (atoi(argv[1]) == 0) {
			//argv1 is not a port, its a flag, therefore use default port
			fprintf(stderr, "No port detected, using default port of '20016'\n");
			port = 20016;
			opt = argv[1];
		} else {
			//argv1 is port, use port
			port = atoi(argv[1]);
		}
	}
	else if (argc == 3) {
		//port and option is specified
		if (atoi(argv[2]) == 0) {
			fprintf(stderr, "Error!, usage is myhttpd <option> <port-number> if option is specified");
			exit (-1);
		}

		opt = argv[1];
		port = atoi(argv[2]);
		
	}
	else {
		fprintf(stderr, "Error!, usage is myhttpd <option> <port-number> where port number and option are optional\n"
				"Options: -f <process mode> , -p <thread pool mode>, -t <thread mode>\n"
				"Port: Where 1024 < port < 65536\n");
		exit (-1);

	}

	  // Set the IP address and port for this server
  	struct sockaddr_in serverIPAddress; 
  	memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  	serverIPAddress.sin_family = AF_INET;
  	serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  	serverIPAddress.sin_port = htons((u_short) port);

	int masterSocket = socket(PF_INET,SOCK_STREAM,0);
		if(masterSocket < 0) {
			perror("Socket");
			exit(-1);
		}
 	// Set socket options to reuse port. Otherwise we will
 	 // have to wait about 2 minutes before reusing the same port number
 	 int optval = 1; 
 	 int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
			       (char *) &optval, sizeof( int ) );
	
	
  	// Bind the socket to the IP address and port
  	int error = bind( masterSocket,
			    (struct sockaddr *)&serverIPAddress,
			    sizeof(serverIPAddress) );
	  if ( error ) {
	    perror("bind");
	    exit( -1 );
	  }

	// Put socket in listening mode and set the 
  	// size of the queue of unprocessed connections

  	error = listen( masterSocket, QueueLength);
  	if ( error ) {
  	  perror("listen");
  	  exit( -1 );
  	}
	//if option is -p (pool of threads)
	if (strcmp(opt, "-p") == 0) {
		pthread_mutexattr_init(&mattr);
		pthread_mutex_init(&mt, &mattr);
		pthread_t tid[5];
		//init the thread
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
		for (int i = 0; i < 5; i++) {
			pthread_create(&tid[i], &attr, (void * (*)(void *))poolSlave, (void*)masterSocket);
		}
		pthread_join(tid[0], NULL);
	}
	else {	
		while(1) {
			// Accept incoming connections
    			struct sockaddr_in clientIPAddress;
    			int alen = sizeof( clientIPAddress );
	
	
			int slaveSocket = accept(masterSocket,
				 (struct sockaddr *) &clientIPAddress,
				 (socklen_t *) &alen);
	
			//check if it is process based (-f)

			if (!strcmp(opt, "-f")) {
				if (slaveSocket == -1 && errno == EINTR) {
					continue;
				}
				pid_t child = fork();
				if (child == 0) {
					processRequest(slaveSocket);
					close(slaveSocket);
					exit(EXIT_SUCCESS);
				}
				//need to close the parent socket as well
				close(slaveSocket);
				//handling the zombie processes
				struct sigaction sa;
				sa.sa_handler = zombieHandler;
				sigemptyset(&sa.sa_mask);
				sa.sa_flags = SA_RESTART;
				if (sigaction(SIGCHLD, &sa, NULL)) {
					perror("sigaction");
				}

			} else if (!strcmp(opt, "-t")) {
				//thread based with option (-t)
				if (slaveSocket < 0) {
					perror("accept");
					exit(-1);
				}
				pthread_t thread;
				pthread_attr_t attr;
				pthread_attr_init(&attr);
				pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
				pthread_create(&thread, &attr, (void *(*)(void*))processThread, (void*)slaveSocket);
			

			}else {
				// Process request.
	    			processRequest( slaveSocket );
		
		    		// Close socket
		    		close( slaveSocket );

			}
		}
	}
}

void
processRequest(int socket) {
	int length = 0;
	char currString[1025];
	char docPath[1025];
	int n;
	char cr = '\r';
	char lf = '\n';
	unsigned char newChar;
	unsigned char lastChar;
	unsigned char lastlastChar;
	unsigned char lastlastlastChar;
	int gotGet = 0;
	int gotDocPath = 0;
	int docIndex = 0;

	while((n = read(socket, &newChar, sizeof(newChar)))){
		//Check if it is CRLF CRLF which ends the header
		if(newChar == lf && lastChar == cr && lastlastChar == lf & lastlastlastChar == cr) {
			length--;
			currString[length] = '\0';
			break;
		}

			currString[length] = newChar;
			length++;
			lastlastlastChar = lastlastChar;
			lastlastChar = lastChar;
			lastChar = newChar;
		
	}
		//got the entire length, now try getting the docname

		int index = 0;
		index += 4;
		char c;
		while((c =  currString[index++]) != ' ') {
			if(index > length) {
				break;
			}
			docPath[docIndex++] = c;
		}
		docPath[docIndex] = '\0';

		//NOW THAT DOCPATH IS GOTTEN,
		//MAP TO REAL PATH

		char * cwd = (char*)malloc(sizeof(char)*256);
		cwd = getcwd(cwd, 256);
		// to get cwd+”http-root-dir/”+docpath 
		
		if(checkPath(docPath, "/htdocs")) {
			strcat(cwd, "/http-root-dir/");
			strcat(cwd,docPath);
		}
		else if (checkPath(docPath, "/icons")) {
			strcat(cwd, "/http-root-dir/");
			strcat(cwd,docPath);
		}
		else if (strlen(docPath) == 1 && docPath[0] == '/') {
			strcat(cwd, "/http-root-dir/htdocs/index.html");
		}
		else {
			strcat(cwd, "/http-root-dir/htdocs");
			strcat(cwd, docPath);
		}

		//check type requested
		char* type;
		if (checkType(cwd, ".gif") || checkType(cwd, ".gif/")) {
			type = strdup("image/gif");
		}
		else if (checkType(cwd, ".html") || checkType(cwd, ".html/")) {
			type = strdup("text/html");
		}
		else {
			type = strdup("text/plain");
		}	
		
		//openFile


		//expand file path
		char * link = cwd;
		char realPath[1024];
		char * ptr = realpath(link, realPath);


		
		FILE * file;
		if(strcmp(type, "image/gif") == 0) {
			file = fopen(ptr, "rb");
		} else {
			file = fopen(ptr, "r");
		}

		if (file == NULL) {
			write(socket, "HTTP/1.0 404 File Not Found", 27);
			write(socket, "\r\n", 2);
			write(socket, "Server: CS252 Lab5", 18);
			write(socket, "\r\n", 2);
			write(socket, "Content-type: ", 14);
			write(socket, type, strlen(type));
			write(socket, "\r\n\r\n", 4);
			write(socket, "File cannot be found\n", 15); 
			return;
		}
		else {
			write(socket, "HTTP/1.0 200 ", 13);
			write(socket, "Document", 8);
			write(socket, " follows\r\n", 10);
			write(socket, "Server: CS252 Lab5", 18);
			write(socket, "\r\n", 2);
			write(socket, "Content-type: ", 14);
			write(socket, type, strlen(type));
			write(socket, "\r\n", 2);
			write(socket, "\r\n", 2);
		}
			char s;
			int count = 0;
			while (count = read(fileno(file), &s, 1)) {
				if (write(socket, &s, 1) != count) {
					perror("write");
				}
			}
			//close the file and socket
			fclose(file);
			close(socket);
}


void poolSlave(int socket) {
	while (1) {
		pthread_mutex_lock(&mt);
		struct sockaddr_in clientIPAddress;
		int alen = sizeof( clientIPAddress);
		int slaveSocket = accept(socket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
		if (slaveSocket < 0) {
			perror("accept");
			exit(-1);
		}
		pthread_mutex_unlock(&mt);
		processRequest(slaveSocket);
		close(slaveSocket);
	}

}

void processThread(int socket) {
	processRequest(socket);
	close(socket);
}

bool checkType(char * dir, char* type) {
	int dirlength = strlen(dir);
	int typelength = strlen(type);
	int typeindex = typelength - 1;
	int dirindex = dirlength - 1;
	int count = 0;
	for (int i = 0; i < typelength; i++) {
		if (dir[dirindex] == type[typeindex]) {
			dirindex--;
			typeindex--;
			count++;
		}
	}
	if (count == typelength) {
		return true;
	}
	return false;
}
bool checkPath(char *dir, char* path) {
	int count = 0;
	for (int i = 0; i < strlen(path); i++) {
		if (dir[i] == path[i]) {
			count++;
		}
	}
	if (count == strlen(path)) {
		return true;
	}
	return false;
}