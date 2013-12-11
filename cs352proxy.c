/* cs352proxy.c
 * Project 2 - Internet Technology - Fall 2013
 * Jan Racoma
 * Objective: Maintaining link states with multiple peers.
 */

#include "cs352proxy.h"
/* Debug */
 int debug = 1;

/* FDs used for read/write to net and tap */
 int tap_fd, net_fd;

/* Socket FD */
 int sock_fd;

/* Local Parameters */
 int linkPeriod, linkTimeout, quitAfter;
 struct peerList *peerHead = NULL;
 struct linkState *local_info;

/* Threads to handle socket and tap */
 pthread_t sleep_thread, listen_thread, connect_thread, socket_thread;
 pthread_mutex_t peer_mutex = PTHREAD_MUTEX_INITIALIZER, linkstate_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Open a tun/tap and return the fd to read/write back to caller */
 int allocate_tunnel(char *dev, int flags) {
 	int fd, error;
 	struct ifreq ifr;
 	char *device_name = "/dev/net/tun";
 	if((fd = open(device_name , O_RDWR)) < 0 ) {
 		perror("error opening /dev/net/tun");
 		return fd;
 	}
 	memset(&ifr, 0, sizeof(ifr));
 	ifr.ifr_flags = flags;

 	if (*dev) {
 		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
 	}

 	if((error = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
 		perror("ioctl on tap failed");
 		close(fd);
 		return error;
 	}

 	strcpy(dev, ifr.ifr_name);

 	return fd;
 }

/* Resolve hostname to a.b.c.d address */
 int getIP(char *host, char *ip) {
 	struct hostent *hp;
 	struct in_addr **addr_list;
 	int i;

 	if ((hp = gethostbyname(host)) == NULL) {
 		perror("gethostname");
 		return 1;
 	}

 	addr_list = (struct in_addr **)hp->h_addr_list;
 	for (i = 0; addr_list[i] != NULL; i++) {
 		strcpy(ip, inet_ntoa(*addr_list[i]));
 		return 0;
 	}

 	return 1;
 }

/* Initiliaze local parameters */
 int initLocalParams() {
 	struct ifreq ifr;
 	char buffer[MAXLINESIZE];
 	char *dev = "tap10";
 	local_info = (struct linkState *)malloc(sizeof(struct linkState));

	/* Template for local linkStatePacket */
 	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
 	ifr.ifr_addr.sa_family = AF_INET;

  /* Obtain local IP address of eth0 */
 	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
 	if (ioctl(sock_fd, SIOCGIFADDR, &ifr) < 0) {
 		perror("ioctl(SIOCGIADDR)");
 		return EXIT_FAILURE;
 	}
 	inet_aton((char *)inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), &local_info->listenIP);

	/* Obtain local MAC ID for tap10 */
	//  strncpy(ifr.ifr_name, "tap10", IFNAMSIZ-1);
	//  if (ioctl(sock_fd, SIOCGIFHWADDR, &ifr) < 0) {
	//    perror("ioctl(SIOCGIFHWADDR)");
	//    return EXIT_FAILURE;
	//  }

	// local_info->ethMAC = (struct sockaddr *)ifr.ifr_hwaddr;

 	sprintf(buffer, "/sys/class/net/%s/address", dev);
 	FILE *f = fopen(buffer, "r");
 	fread(buffer, 1, MAXLINESIZE, f);
 	sscanf(buffer ,"%hhX:%hhX:%hhX:%hhX:%hhX:%hhX", (unsigned char *)&local_info->ethMAC.sa_data[0], (unsigned char *)&local_info->ethMAC.sa_data[1], (unsigned char *)&local_info->ethMAC.sa_data[2], (unsigned char *)&local_info->ethMAC.sa_data[3], (unsigned char *)&local_info->ethMAC.sa_data[4], (unsigned char *)&local_info->ethMAC.sa_data[5]);
 	fclose(f);

 	return 0;
 }


/* Parse through input file */
 int parseInput(int argc, char *argv[]) {
 	FILE *input_file;
	/* Line number of current line being read from input_file */
 	int line_number = 0;
	/* ptr to next field extracted from current line */
 	char *next_field;
	/* ptr to current input  */
 	char line[MAXLINESIZE+1];
	/* Variables for peer information */
 	char *host, *tapDevice;
 	char ip[100];
 	int port, count;
 	struct peerList *current = (struct peerList *)malloc(sizeof(struct peerList));
 	current->lsInfo = (struct linkState *)malloc(sizeof(struct linkState));

	/* Verifies proper syntax command line */
 	if (argc != 2) {
 		puts("Syntax: cs352proxy <input_file>");
 		return EXIT_FAILURE;
 	}

	/* Initialize local parameters */
 	initLocalParams();

	/* Open input file */
 	input_file = fopen(argv[1], "r");
 	if (input_file == NULL) {
 		perror("argv[1]");
 		return EXIT_FAILURE;
 	}

	/* Iterate through the input line one line at a time */
 	while (fgets(line, MAXLINESIZE, input_file)) {
 		line_number++;
 		next_field = strtok(line, " \n");

 		if (!next_field || !strcmp(next_field, "//")) continue;
 		else if (!strcmp(next_field, "listenPort")) local_info->listenPort = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "linkPeriod")) linkPeriod = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "linkTimeout")) linkTimeout = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "quitAfter")) quitAfter = atoi(strtok(NULL, " \n"));
 		else if (!strcmp(next_field, "peer")) {
 			host = strtok(NULL, " \n");

			/* Checks for a.b.c.d address, otherwise resolve hostname */
 			if (inet_addr(host) == -1) {
 				getIP(host, ip);
 				host = ip;
 			}
 			port = atoi(strtok(NULL, " \n"));
 			fgets(line, MAXLINESIZE, input_file);
 			next_field = strtok(line, " \n");
 			tapDevice = strtok(NULL, " \n");
 			inet_aton(host, &current->lsInfo->listenIP);
 			current->lsInfo->listenPort = port;
 			strcpy(current->tapDevice, tapDevice);
 			if (pthread_create(&connect_thread, NULL, connectToPeer, (void *)current) != 0) {
 				perror("connect_thread");
 				pthread_exit(NULL);
 			}
 			pthread_join(connect_thread, NULL);
 		}
 	}

 	if (debug) {
 		puts("\nLocal Information:");
 		print_linkState(local_info);
 		printf("\n---Linked List:\n");
 		LL_COUNT(peerHead, current, count);
 		LL_FOREACH(peerHead, current) {
 			print_linkState(current->lsInfo);
 		}
 		printf("Count: %d\n", count);
 		printf("linkPeriod: %d | linkTimeout: %d | quitAfter: %d\n", linkPeriod, linkTimeout, quitAfter);
 	}

	/* Close input file */
 	fclose(input_file);

 	return 0;
 }

/* Read from socket and write to tap */
 void *handle_listen()
 {
 	int size, new_fd;
 	uint16_t type;
 	char buffer[MAXBUFFSIZE], buffer2[MAXBUFFSIZE];
 	struct sockaddr_in client_addr;
 	socklen_t addrlen = sizeof(client_addr);
 	struct peerList *peer;

 	if (debug) puts("create thread for listening");

		/* Listens for connection, backlog 10 */
 	if (listen(sock_fd, BACKLOG) < 0) {
 		perror("listen");
 		exit(1);
 	}

 	if ((new_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &addrlen)) < 0) {
 		perror("accept");
 		exit(1);
 	}

 	printf("Client connected from %s:%d.\n", inet_ntoa(client_addr.sin_addr), htons(client_addr.sin_port));
 	while (1) {
 		memset(buffer, 0, MAXBUFFSIZE);
 		size = recv(net_fd, buffer, sizeof(buffer), 0);
 		if (size > 0) {
 			if (strlen(buffer) > 0) {
 				strncpy(buffer2, buffer, 6);
 				type = (uint16_t)strtol(buffer2, (char **)&buffer2, 0);
 				printf("TYPE: %x\n", type);
 				switch (type) {
 					case PACKET_LINKSTATE:
 					strncpy(buffer, buffer+7, sizeof(buffer));
 					printf("Received message: %d bytes\n", size);
 					printf("Received: %s\n", buffer);
 					decode_linkStatePacket();
 					default:
 					printf("Negative.\n");
 				}
 			}
 		} else if (size == 0) {
 			pthread_mutex_lock(&peer_mutex);
 			pthread_mutex_lock(&linkstate_mutex);

 			LL_FOREACH(peerHead, peer) {
 				if (peer->net_fd == new_fd) {
 					puts("found");
 				}
 			}
 			close(new_fd);
 			pthread_mutex_unlock(&peer_mutex);
 			pthread_mutex_unlock(&linkstate_mutex);
 		} else {
 			printf("PEER: Peer Removed %s:%d: Peer disconnected\n", inet_ntoa(client_addr.sin_addr), htons(client_addr.sin_port));
 			close(new_fd);
 			break;
 		}

 	}

 	return NULL;
 }

/* Server Mode */
 void server(int port)
 {
 	struct sockaddr_in local_addr;
 	int optval = 1;

		/* Allows reuse of socket if not closed properly */
 	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0) {
 		perror("setsockopt");
 		exit(1);
 	}

		/* Bind socket */
 	memset((char *)&local_addr, 0, sizeof(local_addr));
 	local_addr.sin_family = AF_INET;
 	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 	local_addr.sin_port = htons(port);
 	if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
 		perror("bind");
 		exit(1);
 	}

 	printf("Server Mode: Waiting for connections on %s:%d...\n", inet_ntoa(local_info->listenIP), port);

		/* Wait for connections on a new thread
		* net_fd is new fd to be used for read/write */
 	if (pthread_create(&listen_thread, NULL, handle_listen, NULL) != 0) {
 		perror("listen_thread");
 		exit(1);
 	}
 }

/* Thread to open and handle tap device, read from tap and send to socket */
 void *handle_tap()
 {
 	puts("create thread for tap");
 	uint16_t type;
 	ssize_t size;
 	char *buffer = malloc(MAXBUFFSIZE);
 	union ethframe frame;

 	while (1) {
 		puts("in while tap");
 		size = read(tap_fd, &frame, MAXBUFFSIZE);
 		printf("tapSize: %d\n", size);
		// if(errno!=EINTR) {
		// 	perror("EINTR");
		// 	exit(1);
		// }
 		if (size < 0) {
 			perror("not connected tap");
 			close(net_fd);
 			close(tap_fd);
 			exit(1);
 		} else if (size == 0) {
 			printf("0 tap");
 			close(tap_fd);
 			close(net_fd);
 			exit(1);
 		}
 		type = htons(0xABCD);
 		buffer[size] = '\0';
 		printf("Tap Message: %s | Size: %d bytes.\n", buffer, size);
 		if (write(tap_fd, buffer, sizeof(buffer)) < 0) {
 			perror("write to tap");
 			close(tap_fd);
 			close(net_fd);
 			exit(1);
 		} else {
 			printf("Message sent to socket.\n");
 			close(tap_fd);
 			pthread_exit(NULL);
 		}
 	}
 	return NULL;
 }

/* Client Mode */
 void *connectToPeer(void *temp) {
 	struct sockaddr_in remote_addr;
 	int new_fd;
 	char *buffer = malloc(MAXBUFFSIZE);
 	struct peerList *peer = (struct peerList *)temp;
 	// struct linkState *newLS = (struct linkState *)malloc(sizeof(struct linkState));
 	// struct linkStateSource *lsSource = (struct linkStateSource *)malloc(sizeof(struct linkStateSource));
 	// lsSource->ls = (struct linkState *)malloc(sizeof(struct linkState));
 	struct linkStatePacket *lsPacket = (struct linkStatePacket *)malloc(sizeof(struct linkStatePacket));
 	// struct packetHeader *hdr = (struct packetHeader *)malloc(sizeof(struct packetHeader));
 	lsPacket->header = (struct packetHeader *)malloc(sizeof(struct packetHeader));
 	// lsPacket->source = (struct linkStateSource *)malloc(sizeof(struct linkStateSource));
 	struct timeval current_time;

/* Create TCP Socket */
 	if ((new_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
 		perror("could not create socket");
 		exit(1);
 	}
 	puts("Client Mode:");
 	memset((char *)&remote_addr, 0, sizeof(remote_addr));
 	remote_addr.sin_family = AF_INET;
 	remote_addr.sin_port = htons(peer->lsInfo->listenPort);
 	inet_aton((char *)inet_ntoa(peer->lsInfo->listenIP), &remote_addr.sin_addr);

 	printf("NEW PEER: Connecting to %s:%d\n", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));

/* Connect to server */
 	if ((connect(new_fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr))) != 0) {
 		printf("NEW PEER: Peer Removed %s:%d: Failed to connect\n", inet_ntoa(peer->lsInfo->listenIP), peer->lsInfo->listenPort);
 		pthread_exit(NULL);
 	} else {
 		printf("NEW PEER: Connected to server %s:%d\n", inet_ntoa(peer->lsInfo->listenIP), peer->lsInfo->listenPort);
 	}
/* Create link state packet */
 	gettimeofday(&current_time, NULL);
 	strcpy(buffer, peer->tapDevice);
 	peer->uniqueID = current_time;
 	peer->linkWeight = 1;
 	peer->net_fd = new_fd;
 	peer->next = NULL;
 	pthread_mutex_lock(&peer_mutex);
 	LL_APPEND(peerHead, peer);
 	pthread_mutex_unlock(&peer_mutex);

 	if (debug) print_peerList(peer);

 	lsPacket->header->type = htons(PACKET_LINKSTATE);
 	lsPacket->source = local_info;
 	LL_COUNT(peerHead, peer, lsPacket->neighbors);
 	send_singleLinkStatePacket(lsPacket, new_fd);
 	puts("here");
 	puts("NEW PEER: Single link state record sent.");
 	if (debug) print_linkStatePacket(lsPacket);

 	return NULL;
 }

/* Send linkState */
 char *send_linkState(struct linkState *ls) {
 	char *buffer = malloc(MAXBUFFSIZE);

 	/* Serialize Data - Packet Type | listenIP | listenPort | ethMAC */
 	sprintf(buffer, " %s %d %02x:%02x:%02x:%02x:%02x:%02x", inet_ntoa(ls->listenIP), ntohs(ls->listenPort), (unsigned char)ls->ethMAC.sa_data[0], (unsigned char)ls->ethMAC.sa_data[1], (unsigned char)ls->ethMAC.sa_data[2], (unsigned char)ls->ethMAC.sa_data[3], (unsigned char)ls->ethMAC.sa_data[4], (unsigned char)ls->ethMAC.sa_data[5]);
 	return buffer;
 }

/* Send single linkStatePacket */
 void send_singleLinkStatePacket(struct linkStatePacket *lsp, int new_fd) {
 	char *buffer = malloc(MAXBUFFSIZE);

 	pthread_mutex_lock(&peer_mutex);
 	pthread_mutex_lock(&linkstate_mutex);

 	/* Serialize Data - Packet Type | Packet Length | Source IP | Source Port | Eth MAC | Neighbors */
 	lsp->header->length = sizeof(lsp) + sizeof(lsp->header) + sizeof(lsp->source);
 	sprintf(buffer, "0x%x %d %s %d %02x:%02x:%02x:%02x:%02x:%02x %d", ntohs(lsp->header->type), lsp->header->length, inet_ntoa(lsp->source->listenIP), lsp->source->listenPort, (unsigned char)lsp->source->ethMAC.sa_data[0], (unsigned char)lsp->source->ethMAC.sa_data[1], (unsigned char)lsp->source->ethMAC.sa_data[2], (unsigned char)lsp->source->ethMAC.sa_data[3], (unsigned char)lsp->source->ethMAC.sa_data[4], (unsigned char)lsp->source->ethMAC.sa_data[5], lsp->neighbors);

 	pthread_mutex_unlock(&peer_mutex);
 	pthread_mutex_unlock(&linkstate_mutex);

	send(new_fd, buffer, strlen(buffer), 0);
 	if (debug) printf("\nPAYLOAD SENT: %s on %d\n\n", buffer, new_fd);
 }

/* Send linkStatePacket */
 void send_linkStatePacket(struct linkStatePacket *lsp) {
 	char *buffer = malloc(MAXBUFFSIZE);
 	struct peerList *peer;

 	pthread_mutex_lock(&peer_mutex);
 	pthread_mutex_lock(&linkstate_mutex);
 	LL_FOREACH(peerHead, peer) {
 		// if (lsp->uniqueID.tv_sec == peer->uniqueID.tv_sec && lsp->uniqueID.tv_usec == peer->uniqueID.tv_usec) {
 			// break;
 		// }
 	}

 	/* Serialize Data - Packet Type | Packet Length | Source IP | Source Port | Eth MAC | Neighbors | UniqueID | linkWeight */
 	// lsp->header->length = sizeof(lsp);
 	// sprintf(buffer, "0x%x %x %s %d %02x:%02x:%02x:%02x:%02x:%02x %d %ld:%ld %d", ntohs(lsp->header->type), lsp->header->length, inet_ntoa(lsp->source->ls->listenIP), ntohs(lsp->source->ls->listenPort), (unsigned char)lsp->source->ls->ethMAC.sa_data[0], (unsigned char)lsp->source->ls->ethMAC.sa_data[1], (unsigned char)lsp->source->ls->ethMAC.sa_data[2], (unsigned char)lsp->source->ls->ethMAC.sa_data[3], (unsigned char)lsp->source->ls->ethMAC.sa_data[4], (unsigned char)lsp->source->ls->ethMAC.sa_data[5], lsp->source->neighbors, lsp->uniqueID.tv_sec, lsp->uniqueID.tv_usec, lsp->linkWeight);

 	/* Add proxy information */
 	// printf("SENT: %s | Length: %d\n", buffer, strlen(buffer));
 	// send(peer->net_fd, buffer, strlen(buffer), 0);
 	pthread_mutex_unlock(&peer_mutex);
 	pthread_mutex_unlock(&linkstate_mutex);
 }

/* Print packetHeader information */
 void print_packetHeader(struct packetHeader *pkt) {
 	printf("---PACKETHEADER: Type: 0x%x | Length: %d\n", ntohs(pkt->type), ntohs(pkt->length));
 }

/* Print peerList information */
 void print_peerList(struct peerList *peer) {
 	printf("---PEERLIST: ");
 	print_linkState(peer->lsInfo);
 	printf("----Tap: %s | UID: %ld:%ld | LinkWeight: %d | NET_FD: %d ", peer->tapDevice, peer->uniqueID.tv_sec, peer->uniqueID.tv_usec, peer->linkWeight, peer->net_fd);
 	if (peer->next == NULL) {
 		printf("Next: NULL\n");
 	} else {
 		printf("Next: %s:%d\n", inet_ntoa(peer->next->lsInfo->listenIP), peer->next->lsInfo->listenPort);
 	}
 }

/* Print linkState information */
 void print_linkState(struct linkState *ls) {
 	char ethMAC[19];
 	sprintf(ethMAC, "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned char)ls->ethMAC.sa_data[0], (unsigned char)ls->ethMAC.sa_data[1], (unsigned char)ls->ethMAC.sa_data[2], (unsigned char)ls->ethMAC.sa_data[3], (unsigned char)ls->ethMAC.sa_data[4], (unsigned char)ls->ethMAC.sa_data[5]);
 	printf("---LINKSTATE: listenIP: %s:%d | MAC: %s\n", inet_ntoa(ls->listenIP), ls->listenPort, ethMAC);
 }

/* Print linkStatePacket information */
 void print_linkStatePacket(struct linkStatePacket *lsp) {
 	int count, i = 1;
 	struct peerList *tmp;
 	puts("---LINKSTATE PACKET INFORMATION---");
 	print_packetHeader(lsp->header);
 	print_linkState(lsp->source);
 	printf("----Neighbors: %d\n", lsp->neighbors);
 	LL_COUNT(peerHead, tmp, count);
 	if (count > 0 ) {
 		LL_FOREACH(peerHead, tmp) {
 			printf("-----PROXY %d-----\n", i);
 			print_peerList(tmp);
 		}
 	}
 }

/* Decode linkStatePacket information */
 struct linkStatePacket decode_linkStatePacket() {
 	struct linkStatePacket test;
 	puts("HERE!");
 	return test;
 }

/* Sleeper for quitAfter */
 void *sleeper() {
 	// sleep(quitAfter);
 	sleep(20);
 	printf("%d seconds have elapsed. Program terminating.\n", quitAfter);
 	exit(1);
 }

/* Main */
 int main (int argc, char *argv[]) {
 	if (debug) {
 		puts("DEBUGGING MODE:");
 	}

 // 	struct timeval test;
 // 	gettimeofday(&test, NULL);
 // 	if (debug) {
 // 		printf("Time of Day: %ld:%ld\n", test.tv_sec, test.tv_usec);
 // 	}

 // 	int size;
 // 	char if_name[IFNAMSIZ] = "";
 // 	unsigned char dest[ETH_ALEN] = { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 };
 // 	unsigned short proto = 0x1234;
 // 	char *data = "hello world~!!!";
 // 	unsigned short data_len = strlen(data);

 // 	unsigned char source[ETH_ALEN] = { 0x61, 0x12, 0x34, 0x56, 0x78, 0x90 };

 // 	union ethframe frame;
 // 	memcpy(frame.field.header.h_dest, dest, ETH_ALEN);
 // 	memcpy(frame.field.header.h_source, source, ETH_ALEN);
 // 	frame.field.header.h_proto = htons(proto);
 // 	memcpy(frame.field.data, data, data_len);

 // 	unsigned int frame_len = data_len + ETH_HLEN;
 // 	strncpy(if_name, "tap10", IFNAMSIZ - 1);
 // 	printf("Attempting to open %s...\n", if_name);
	// /* Open tap interface */
 // 	if ((tap_fd = allocate_tunnel(if_name, IFF_TAP | IFF_NO_PI)) < 0) {
 // 		perror("Opening tap interface failed!");
 // 		return EXIT_FAILURE;
 // 	} else {
 // 		printf("Successfully opened %s interface...\n", if_name);
 // 	}

 // 	if ((size = write(tap_fd, &frame.buffer, frame_len)) < 0) {
 // 		perror("write to tap");
 // 		close(tap_fd);
 // 		return EXIT_FAILURE;
 // 	} else {
 // 		printf("%d bytes sent to tap..\n", size);

 // 	}

 // 	if (pthread_create(&socket_thread, NULL, handle_tap, NULL) != 0 ) {
 // 		perror("socket_thread");
 // 		return EXIT_FAILURE;
 // 	}

	/* Parse input file */
 	if (parseInput(argc, argv)) {
 		perror("parseInput");
 		close(tap_fd);
 		return EXIT_FAILURE;
 	}

 	/* Set quitAfter sleeper */
 	if (pthread_create(&sleep_thread, NULL, sleeper, NULL)) {
 		perror("connect thread");
 		pthread_exit(NULL);
 	}

	/* Start server path */
 	server(local_info->listenPort);

 	close(tap_fd);
 	pthread_exit(NULL);

 	return 0;
 }
